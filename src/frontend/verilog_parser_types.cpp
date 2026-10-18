// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

#include <utility>
#include <bit>

namespace fsim::frontend {

Type VerilogParser::parse_systemverilog_aggregate_type(bool* const complete)
{
    const bool is_union = match_keyword("union");
    if (!is_union) {
        (void)match_keyword("struct");
    }
    bool tagged = is_union && match_keyword("tagged");
    const bool packed = match_keyword("packed");
    if (is_union && !tagged && match_keyword("tagged")) {
        tagged = true;
    }
    Type type;
    type.spelling = tagged ? "union tagged packed"
        : is_union         ? packed ? "union packed" : "union"
        : packed           ? "struct packed"
                           : "struct";
    type.packed_aggregate = tagged
        ? PackedAggregateKind::TaggedUnion
        : is_union
        ? packed ? PackedAggregateKind::Union
                 : PackedAggregateKind::UnpackedUnion
        : packed ? PackedAggregateKind::Struct
                 : PackedAggregateKind::UnpackedStruct;
    type.domain = ValueDomain::Bit2;
    if (packed) {
        parse_optional_signedness(type);
    }
    if (!at(TokenKind::LeftBrace)) {
        (void)expect(
            TokenKind::LeftBrace,
            "'{' before aggregate members",
            "FSIM-SV-PARSE-086");
        skip_to_semicolon();
        if (complete != nullptr) {
            *complete = false;
        }
        return type;
    }
    (void)advance();
    if (at(TokenKind::RightBrace)) {
        error(
            current(),
            "FSIM-SV-PARSE-089",
            "bounded aggregates require at least one member");
    }
    std::unordered_set<std::string> member_names;
    std::optional<std::uint64_t> ordinary_union_width;
    while (!at_end() && !at(TokenKind::RightBrace)) {
        const auto member_progress = position();
        // Members of an unpacked structure may be random variables
        // (IEEE 1800-2017 7.2.1, 18.4).
        if (!packed) {
            (void)(match_keyword("rand") || match_keyword("randc"));
        }
        const auto member_start = current();
        Type member_type;
        if (tagged && keyword("void")) {
            // A void member of a tagged union carries no value (IEEE
            // 1800-2017 7.3.2); it is held as one unused bit.
            (void)advance();
            member_type = Type { ValueDomain::Bit2, "bit",
                PackedRange { 0, 0, true }, false };
        } else if (keyword("struct") || keyword("union")) {
            member_type = parse_systemverilog_aggregate_type();
        } else if (keyword("enum")) {
            // An anonymous enumeration member declares its literals in the
            // scope of the aggregate's declaration (IEEE 1800-2017 7.2).
            std::vector<EnumLiteralDeclaration> literals;
            member_type = parse_systemverilog_enum_type(&literals);
            member_enumerations_.emplace_back(member_type, std::move(literals));
        } else if (auto named_array = parse_named_packed_array_type()) {
            // A packed array of a named packed type (`inner_t [1:0] e;`,
            // IEEE 1800-2017 7.4.1).
            member_type = std::move(*named_array);
        } else if (!packed) {
            member_type = parse_parameter_type();
        } else if (keyword("logic") || keyword("reg")
            || keyword("bit") || keyword("byte")
            || keyword("shortint") || keyword("int")
            || keyword("longint") || keyword("integer")
            || keyword("time")) {
            member_type = parse_parameter_type();
        } else if (is_named_type_reference_start()) {
            member_type = parse_named_type();
        } else {
            error(
                current(),
                "FSIM-SV-UNSUPPORTED-028",
                "bounded aggregate members require an executable integral, "
                "nested aggregate, or visible named type");
            skip_to_semicolon();
            continue;
        }
        if (packed
            && (member_type.packed_aggregate
                    == PackedAggregateKind::UnpackedStruct
                || member_type.packed_aggregate
                    == PackedAggregateKind::UnpackedUnion)) {
            error(
                member_start,
                "FSIM-SV-UNSUPPORTED-028",
                "a packed aggregate cannot contain an unpacked aggregate "
                "member");
        }
        for (;;) {
            const auto member = expect_identifier("aggregate member name");
            auto declarator_type = member_type;
            if (at(TokenKind::LeftBracket)) {
                if (packed) {
                    error(
                        current(),
                        "FSIM-SV-UNSUPPORTED-029",
                        "packed aggregate members cannot have unpacked dimensions");
                    skip_balanced(
                        TokenKind::LeftBracket,
                        TokenKind::RightBracket);
                } else {
                    (void)parse_optional_container_dimension(declarator_type);
                }
            }
            std::optional<Expression> member_initializer;
            if (match(TokenKind::Assign)) {
                // Only unpacked structure members take defaults (7.2.2).
                if (packed) {
                    error(previous(), "FSIM-SV-SEM-401",
                        "a packed structure member cannot have a default value");
                }
                member_initializer = parse_expression();
            }
            if (!member_names.insert(member.text).second) {
                error(
                    member,
                    "FSIM-SV-SEM-025",
                    "duplicate aggregate member '" + member.text + "'");
            } else {
                auto packed_member = PackedMember {
                    member.text,
                    declarator_type.domain,
                    declarator_type.spelling,
                    declarator_type.packed_range,
                    declarator_type.is_signed,
                    declarator_type.packed_range_expression,
                    0,
                    cover(member_start.span, previous().span),
                    { },
                    std::move(member_initializer)
                };
                if (!packed || !declarator_type.named_type.empty()
                    || declarator_type.packed_aggregate
                        != PackedAggregateKind::None
                        // `bit [2:0][3:0] y`: a multidimensional packed member keeps
                        // its dimensions (IEEE 1800-2017 7.4.1).
                        || declarator_type.systemverilog_packed_dimensions.size() > 1U) {
                    packed_member.nested_types.push_back(declarator_type);
                }
                type.packed_members.push_back(std::move(packed_member));
                if (packed && is_union && !tagged) {
                    const auto width = type.packed_members.back().width();
                    if (width && *width != 0U) {
                        if (ordinary_union_width
                            && *ordinary_union_width != *width) {
                            error(
                                member,
                                "FSIM-SV-SEM-242",
                                "all members of an ordinary packed union "
                                "must have the same width");
                        } else {
                            ordinary_union_width = *width;
                        }
                    }
                }
                if (declarator_type.domain == ValueDomain::Logic9) {
                    type.domain = ValueDomain::Logic9;
                } else if (declarator_type.domain == ValueDomain::Logic4
                    && type.domain != ValueDomain::Logic9) {
                    type.domain = ValueDomain::Logic4;
                }
            }
            if (!match(TokenKind::Comma)) {
                break;
            }
        }
        expect(
            TokenKind::Semicolon,
            "';' after aggregate member declaration",
            "FSIM-SV-PARSE-088");
        if (position() == member_progress) {
            skip_to_semicolon();
            if (position() == member_progress) {
                (void)advance();
            }
        }
    }
    expect(
        TokenKind::RightBrace,
        "'}' after aggregate members",
        "FSIM-SV-PARSE-087");
    std::uint64_t total_width = 0;
    std::optional<std::uint64_t> union_width;
    bool concrete = !type.packed_members.empty();
    for (const auto& member : type.packed_members) {
        const auto width = member.width();
        if (!width || *width == 0) {
            concrete = false;
            break;
        }
        if (is_union) {
            union_width = std::max(union_width.value_or(0), *width);
            total_width = *union_width;
        } else if (*width
            > std::numeric_limits<std::uint64_t>::max()
                - total_width) {
            concrete = false;
            break;
        } else {
            total_width += *width;
        }
    }
    const auto tag_width = tagged && !type.packed_members.empty()
        ? std::max<std::uint64_t>(
              1U,
              static_cast<std::uint64_t>(
                  std::bit_width(type.packed_members.size() - 1U)))
        : 0U;
    if (concrete && total_width != 0U
        && tag_width
            <= static_cast<std::uint64_t>(
                   std::numeric_limits<std::int64_t>::max())
                - (total_width - 1U)
        && total_width + tag_width - 1U
            <= static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
        if (!is_union) {
            auto offset = total_width;
            for (auto& member : type.packed_members) {
                offset -= *member.width();
                member.lsb_offset = offset;
            }
        }
        type.packed_range = PackedRange {
            static_cast<std::int64_t>(total_width + tag_width - 1U),
            0,
            true
        };
    }
    // `struct packed { ... } [d] name` is a packed array of the aggregate
    // (IEEE 1800-2017 7.4.1).
    if (packed && type.packed_range && at(TokenKind::LeftBracket)) {
        return packed_array_of_type(type, current().span);
    }
    if (packed && !is_union && !type.packed_range
        && at(TokenKind::LeftBracket)) {
        auto element = type;
        if (aggregate_width_expression(element, current().span)) {
            return packed_array_of_type(element, current().span);
        }
    }
    return type;
}

Type VerilogParser::parse_systemverilog_enum_type(
    std::vector<EnumLiteralDeclaration>* retained_literals)
{
    (void)match_keyword("enum");
    Type type;
    if (keyword("logic") || keyword("reg") || keyword("bit")
        || keyword("byte") || keyword("shortint")
        || keyword("int") || keyword("longint")
        || keyword("integer") || keyword("time")) {
        type = parse_parameter_type();
    } else if (keyword("signed") || keyword("unsigned")
        || at(TokenKind::LeftBracket)) {
        type.domain = ValueDomain::Logic4;
        type.spelling = "logic";
        parse_optional_signedness(type);
        parse_optional_range(type);
    } else if (keyword("string") || keyword("chandle")
        || keyword("process")
        || keyword("shortreal") || keyword("real")
        || keyword("realtime")) {
        const auto unsupported = advance();
        error(
            unsupported,
            "FSIM-SV-UNSUPPORTED-026",
            "an enum base type must be integral");
        (void)apply_systemverilog_integral_type(type, "int");
    } else if (at(TokenKind::Identifier)) {
        type = parse_named_type();
        // A base typedef already known as an integral type supplies the
        // enumeration's representation directly (6.19); otherwise the name
        // is resolved, and its legality checked, during elaboration.
        const auto known = packed_typedef_types_.find(type.named_type);
        if (known != packed_typedef_types_.end()
            && !at(TokenKind::LeftBracket)
            && known->second.named_type.empty()
            && known->second.enumeration_literals.empty()
            && known->second.packed_members.empty()
            && !known->second.systemverilog_container
            && (known->second.domain == ValueDomain::Logic4
                || known->second.domain == ValueDomain::Bit2
                || known->second.domain == ValueDomain::Integer)) {
            type = known->second;
        } else {
            type.systemverilog_enum_base_type = std::move(type.named_type);
            type.named_type.clear();
        }
        // enum_base_type ::= type_identifier [packed_dimension] (6.19).
        if (at(TokenKind::LeftBracket)) {
            parse_optional_range(type);
        }
    } else {
        (void)apply_systemverilog_integral_type(type, "int");
    }
    expect(
        TokenKind::LeftBrace,
        "'{' before enum literals",
        "FSIM-SV-PARSE-083");
    if (at(TokenKind::RightBrace)) {
        error(
            current(),
            "FSIM-SV-PARSE-085",
            "bounded enum declarations require at least one literal");
    }
    std::optional<std::string> previous_literal;
    while (!at_end() && !at(TokenKind::RightBrace)) {
        const auto literal = expect_identifier("enum literal name");
        // `name[N]` declares name0 .. name(N-1) and `name[N:M]` declares
        // nameN .. nameM; an initializer applies to the first
        // (IEEE 1800-2017 6.19.2).
        std::vector<std::string> names;
        if (match(TokenKind::LeftBracket)) {
            const auto decimal = [&]() -> std::optional<std::int64_t> {
                const auto bound = parse_expression();
                if (bound.kind != ExpressionKind::IntegerLiteral
                    || bound.text.empty()
                    || !std::ranges::all_of(bound.text, [](const char c) {
                           return (c >= '0' && c <= '9') || c == '_';
                       })) {
                    error(previous(), "FSIM-SV-PARSE-388",
                        "an enum literal range bound must be an unsized "
                        "decimal integer literal");
                    return std::nullopt;
                }
                std::int64_t value { };
                for (const auto c : bound.text) {
                    if (c != '_') {
                        value = value * 10 + (c - '0');
                    }
                    if (value > 65536) {
                        error(previous(), "FSIM-SV-PARSE-388",
                            "an enum literal range exceeds 65536 literals");
                        return std::nullopt;
                    }
                }
                return value;
            };
            const auto first = decimal();
            std::optional<std::int64_t> last;
            if (match(TokenKind::Colon)) {
                last = decimal();
            }
            expect(TokenKind::RightBracket,
                "']' after enum literal range", "FSIM-SV-PARSE-084");
            if (first && (last || *first > 0)) {
                const auto low = last ? *first : 0;
                const auto high = last ? *last : *first - 1;
                const auto step = high >= low ? 1 : -1;
                for (auto index = low;; index += step) {
                    names.push_back(literal.text + std::to_string(index));
                    if (index == high) {
                        break;
                    }
                }
            }
        } else {
            names.push_back(literal.text);
        }
        std::optional<Expression> explicit_value;
        if (match(TokenKind::Assign)) {
            explicit_value = parse_expression();
        }
        for (std::size_t name_index { }; name_index < names.size();
             ++name_index) {
        const auto& literal_name = names[name_index];
        Expression value;
        if (name_index == 0U && explicit_value) {
            value = *explicit_value;
        } else if (!previous_literal) {
            value = Expression {
                ExpressionKind::IntegerLiteral, "0", { }, literal.span
            };
        } else {
            value = Expression {
                ExpressionKind::Binary,
                "+",
                {
                    Expression {
                        ExpressionKind::Identifier,
                        *previous_literal,
                        { },
                        literal.span },
                    Expression {
                        ExpressionKind::IntegerLiteral,
                        "1",
                        { },
                        literal.span },
                },
                literal.span
            };
        }
        type.enumeration_literals.push_back(literal_name);
        type.systemverilog_enumeration_values.push_back(value);
        if (retained_literals != nullptr) {
            retained_literals->push_back({ literal_name,
                std::move(value),
                cover(literal.span, previous().span) });
        }
        previous_literal = literal_name;
        }
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    expect(
        TokenKind::RightBrace,
        "'}' after enum literals",
        "FSIM-SV-PARSE-084");
    return type;
}

std::optional<Type> VerilogParser::parse_named_packed_array_type()
{
    // `pkg::name [d]` names a package typedef (IEEE 1800-2017 26.3).
    const auto qualified = at(TokenKind::Identifier)
        && at(TokenKind::Scope, 1) && at(TokenKind::Identifier, 2)
        && at(TokenKind::LeftBracket, 3);
    if (!qualified
        && (!at(TokenKind::Identifier) || !at(TokenKind::LeftBracket, 1))) {
        return std::nullopt;
    }
    const auto found = packed_typedef_types_.find(qualified
            ? current().text + "::" + current(2).text
            : current().text);
    if (found == packed_typedef_types_.end()) {
        return std::nullopt;
    }
    // A packed array of an enumeration holds base-type elements.
    auto element = found->second;
    if (!element.enumeration_literals.empty() && element.named_type.empty()
        && element.packed_members.empty()
        && element.systemverilog_enum_base_type.empty()) {
        element.enumeration_literals.clear();
        element.systemverilog_enumeration_values.clear();
    }
    if (!element.named_type.empty() || !element.enumeration_literals.empty()
        || element.systemverilog_container
        || (element.domain != ValueDomain::Logic4
            && element.domain != ValueDomain::Bit2)
        || (element.packed_members.empty() && element.spelling != "logic"
            && element.spelling != "bit" && element.spelling != "reg")) {
        return std::nullopt;
    }
    if (qualified) {
        index_ += 2U;
    }
    const auto name = advance();
    return packed_array_of_type(std::move(element), name.span);
}

bool VerilogParser::aggregate_width_expression(
    Type& type, const SourceSpan& span) const
{
    // A packed structure whose members have parameter-dependent ranges
    // spans [sum of member widths - 1 : 0], folded at elaboration.
    if (type.packed_range || type.packed_range_expression
        || type.packed_aggregate != PackedAggregateKind::Struct
        || type.packed_members.empty()) {
        return false;
    }
    const auto literal = [&](const std::uint64_t value) {
        return Expression { ExpressionKind::IntegerLiteral,
            std::to_string(value), { }, span };
    };
    const auto binary = [&](std::string op, Expression lhs, Expression rhs) {
        return Expression { ExpressionKind::Binary, std::move(op),
            { std::move(lhs), std::move(rhs) }, span };
    };
    std::optional<Expression> total;
    for (const auto& member : type.packed_members) {
        std::optional<Expression> width;
        if (const auto concrete_width = member.width()) {
            width = literal(*concrete_width);
        } else if (member.packed_range_expression
            && member.nested_types.empty()) {
            const auto& range = *member.packed_range_expression;
            width = Expression { ExpressionKind::Call, "?:",
                { binary(">=", range.left, range.right),
                    binary("+", binary("-", range.left, range.right),
                        literal(1U)),
                    binary("+", binary("-", range.right, range.left),
                        literal(1U)) },
                span };
        } else {
            return false;
        }
        total = total ? binary("+", std::move(*total), std::move(*width))
                      : std::move(*width);
    }
    type.packed_range_expression = PackedRangeExpression {
        binary("-", std::move(*total), literal(1U)), literal(0U), span,
        std::nullopt };
    return true;
}

Type VerilogParser::packed_array_of_type(
    const Type& element, const SourceSpan& span)
{
    Type outer;
    parse_optional_range(outer);
    std::vector<PackedRangeExpression> dimensions
        = outer.systemverilog_packed_dimensions;
    if (outer.packed_range_expression) {
        dimensions.push_back(*outer.packed_range_expression);
    }
    if (!element.systemverilog_packed_dimensions.empty()) {
        dimensions.insert(dimensions.end(),
            element.systemverilog_packed_dimensions.begin(),
            element.systemverilog_packed_dimensions.end());
    } else if (element.packed_range_expression) {
        dimensions.push_back(*element.packed_range_expression);
    } else if (element.packed_range) {
        const auto literal = [&](const std::int64_t value) {
            return Expression { ExpressionKind::IntegerLiteral,
                std::to_string(value), { }, span };
        };
        dimensions.push_back(PackedRangeExpression {
            literal(element.packed_range->left),
            literal(element.packed_range->right), span, std::nullopt });
    }
    auto type = element;
    type.packed_range.reset();
    type.packed_range_expression.reset();
    type.systemverilog_packed_dimensions.clear();
    // A packed array of a named type is unsigned as a whole, whatever its
    // element's signedness (IEEE 1800-2017 7.4.1).
    type.is_signed = false;
    std::uint64_t flattened = 1U;
    bool concrete = true;
    for (const auto& dimension : dimensions) {
        const auto left = simple_verilog_integer_constant(dimension.left);
        const auto right = simple_verilog_integer_constant(dimension.right);
        if (!left || !right) {
            concrete = false;
            continue;
        }
        flattened *= PackedRange { *left, *right, *left >= *right }.width();
    }
    if (dimensions.size() == 1U) {
        type.packed_range_expression = dimensions.front();
        const auto left = simple_verilog_integer_constant(dimensions.front().left);
        const auto right = simple_verilog_integer_constant(dimensions.front().right);
        if (left && right) {
            type.packed_range = PackedRange { *left, *right, *left >= *right };
        }
        return type;
    }
    if (!concrete) {
        // Parameter-dependent bounds: the flattened range is
        // [product of (|left - right| + 1) - 1 : 0], folded at elaboration.
        const auto binary = [&](std::string op, Expression lhs,
                                Expression rhs) {
            return Expression { ExpressionKind::Binary, std::move(op),
                { std::move(lhs), std::move(rhs) }, span };
        };
        const auto one = Expression { ExpressionKind::IntegerLiteral, "1",
            { }, span };
        std::optional<Expression> product;
        for (const auto& dimension : dimensions) {
            auto extent = Expression { ExpressionKind::Call, "?:",
                { binary(">=", dimension.left, dimension.right),
                    binary("+", binary("-", dimension.left, dimension.right),
                        one),
                    binary("+", binary("-", dimension.right, dimension.left),
                        one) },
                span };
            product = product
                ? binary("*", std::move(*product), std::move(extent))
                : std::move(extent);
        }
        type.packed_range_expression = PackedRangeExpression {
            binary("-", std::move(*product), one),
            Expression { ExpressionKind::IntegerLiteral, "0", { }, span },
            span, std::nullopt };
    }
    type.systemverilog_packed_dimensions = std::move(dimensions);
    if (concrete) {
        type.packed_range = PackedRange {
            static_cast<std::int64_t>(flattened - 1U), 0, true };
    }
    return type;
}

void VerilogParser::parse_typedef(
    DesignUnit& unit,
    const Token& start)
{
    Type type;
    std::vector<std::pair<
        ParameterDeclaration,
        Token>>
        enum_parameters;
    std::vector<EnumLiteralDeclaration> enum_literals;
    // `typedef interface class name;` forward-declares an interface class.
    if (keyword("interface") && keyword("class", 1)
        && at(TokenKind::Identifier, 2) && at(TokenKind::Semicolon, 3)) {
        index_ += 4U;
        return;
    }
    // A forward type declaration (IEEE 1800-2017 6.18) names a type that a
    // later typedef completes: `typedef struct name;`, `typedef name;`.
    if (((keyword("struct") || keyword("union") || keyword("enum"))
            && at(TokenKind::Identifier, 1) && at(TokenKind::Semicolon, 2))
        || (at(TokenKind::Identifier) && at(TokenKind::Semicolon, 1))) {
        if (!at(TokenKind::Identifier)
            || keyword("struct") || keyword("union") || keyword("enum")) {
            (void)advance();
        }
        const auto forward_name = advance();
        (void)advance();
        if (compilation_unit_package_ && &unit == &*compilation_unit_package_) {
            compilation_unit_forward_types_.push_back(forward_name);
        }
        return;
    }
    if (keyword("struct") || keyword("union")) {
        bool complete = true;
        type = parse_systemverilog_aggregate_type(&complete);
        if (!complete) {
            return;
        }
    } else if (keyword("enum")) {
        type = parse_systemverilog_enum_type(&enum_literals);
        for (const auto& literal : enum_literals) {
            enum_parameters.push_back({ ParameterDeclaration {
                                            literal.name,
                                            type,
                                            literal.value,
                                            true,
                                            literal.span,
                                            ParameterKind::Value,
                                            std::nullopt },
                Token {
                    TokenKind::Identifier, literal.name, literal.span, { } } });
        }
    } else if (keyword("shortreal") || keyword("real")
        || keyword("realtime") || keyword("time")
        || keyword("chandle") || keyword("process") || keyword("string")
        || keyword("logic") || keyword("reg") || keyword("bit")
        || keyword("byte") || keyword("shortint") || keyword("int")
        || keyword("longint") || keyword("integer")) {
        type = parse_parameter_type();
    } else if (auto expanded = parse_named_packed_array_type()) {
        type = std::move(*expanded);
    } else if (is_named_type_reference_start()) {
        type = parse_named_type();
        // A typedef of a structure or union typedef of this unit takes the
        // aggregate's own description (IEEE 1800-2017 6.18).
        if (type.named_type.find("::") == std::string::npos) {
            const auto aliased = std::ranges::find(unit.type_aliases,
                type.named_type, &TypeAliasDeclaration::name);
            if (aliased != unit.type_aliases.end()
                && !aliased->type.packed_members.empty()) {
                type = aliased->type;
            }
        }
    } else if (keyword("virtual")) {
        // A virtual interface type (IEEE 1800-2017 25.9).
        const auto virtual_start = advance();
        type = parse_virtual_interface_type(virtual_start);
    } else if (auto imported = import_interface_type(unit)) {
        type = std::move(*imported);
    } else {
        error(
            current(),
            "FSIM-SV-UNSUPPORTED-024",
            "bounded typedef declarations require an integral built-in "
            "or previously declared user type");
        skip_to_semicolon();
        return;
    }
    const auto name = expect_identifier("typedef name");
    (void)parse_optional_container_dimension(type);
    expect(
        TokenKind::Semicolon,
        "';' after typedef declaration",
        "FSIM-SV-PARSE-082");
    const bool duplicate = std::any_of(
        unit.type_aliases.begin(),
        unit.type_aliases.end(),
        [&](const TypeAliasDeclaration& alias) {
            return alias.name == name.text;
        });
    const bool type_parameter_conflict = std::ranges::any_of(
        unit.parameters,
        [&](const ParameterDeclaration& parameter) {
            return parameter.kind == ParameterKind::Type
                && parameter.name == name.text;
        });
    if (type_parameter_conflict) {
        error(
            name,
            "FSIM-SV-SEM-055",
            "typedef declaration '" + name.text
                + "' conflicts with a type parameter");
        return;
    }
    if (duplicate) {
        error(
            name,
            "FSIM-SV-SEM-024",
            "duplicate typedef declaration '" + name.text + "'");
        return;
    }
    packed_typedef_types_.insert_or_assign(name.text, type);
    if (unit.kind == UnitKind::SystemVerilogPackage) {
        packed_typedef_types_.insert_or_assign(
            unit.name + "::" + name.text, type);
    }
    unit.type_aliases.push_back({ name.text,
        std::move(type),
        cover(start.span, previous().span),
        std::move(enum_literals),
        TypeDeclarationKind::SystemVerilogTypedef,
        { },
        { },
        { },
        false });
    for (auto& [parameter, parameter_name] :
        enum_parameters) {
        add_parameter(
            unit, std::move(parameter), parameter_name);
    }
    declare_member_enumerations(unit);
}

void VerilogParser::declare_member_enumerations(DesignUnit& unit)
{
    auto enumerations = std::exchange(member_enumerations_, { });
    for (auto& [type, literals] : enumerations) {
        if (literals.empty()) {
            continue;
        }
        for (const auto& literal : literals) {
            add_parameter(unit,
                ParameterDeclaration { literal.name, type, literal.value,
                    true, literal.span, ParameterKind::Value, std::nullopt },
                Token { TokenKind::Identifier, literal.name, literal.span,
                    { } });
        }
        // A hidden typedef gives the enumeration its methods (6.19.5).
        const auto span = literals.front().span;
        unit.type_aliases.push_back({
            anonymous_enumeration_type_name(literals.front().name),
            std::move(type),
            span,
            std::move(literals),
            TypeDeclarationKind::SystemVerilogTypedef,
            { },
            { },
            { },
            false });
    }
}

void VerilogParser::parse_nettype(
    DesignUnit& unit,
    const Token& start)
{
    auto type = parse_parameter_type();
    const auto name = expect_identifier("nettype name");
    std::string resolution_function;
    if (match_keyword("with")) {
        const auto first = expect_identifier("nettype resolution function");
        resolution_function = first.text;
        bool qualified = false;
        while (match(TokenKind::Scope)) {
            qualified = true;
            resolution_function += "::";
            resolution_function += expect_identifier(
                "selected nettype resolution function")
                                       .text;
        }
        if (!qualified
            && unit.kind == UnitKind::SystemVerilogPackage) {
            resolution_function = unit.name + "::" + resolution_function;
        }
    }
    expect(
        TokenKind::Semicolon,
        "';' after nettype declaration",
        "FSIM-SV-PARSE-283");

    const bool duplicate = std::ranges::any_of(
        unit.type_aliases,
        [&](const TypeAliasDeclaration& declaration) {
            return declaration.name == name.text;
        });
    const bool type_parameter_conflict = std::ranges::any_of(
        unit.parameters,
        [&](const ParameterDeclaration& parameter) {
            return parameter.kind == ParameterKind::Type
                && parameter.name == name.text;
        });
    if (duplicate || type_parameter_conflict) {
        error(
            name,
            "FSIM-SV-SEM-237",
            "duplicate or conflicting nettype declaration '"
                + name.text + "'");
        return;
    }

    type.systemverilog_net_type = name.text;
    type.systemverilog_resolution_function = resolution_function;
    TypeAliasDeclaration declaration {
        name.text,
        std::move(type),
        cover(start.span, previous().span),
        { },
        TypeDeclarationKind::SystemVerilogNettype,
        { },
        { },
        { },
        false
    };
    declaration.systemverilog_resolution_function = std::move(resolution_function);
    unit.type_aliases.push_back(std::move(declaration));
}

void VerilogParser::parse_alias_statement(
    DesignUnit& unit,
    const Token& start)
{
    SystemVerilogAliasDeclaration declaration;
    declaration.terminals.push_back(parse_lvalue());
    while (match(TokenKind::Assign)) {
        declaration.terminals.push_back(parse_lvalue());
    }
    expect(
        TokenKind::Semicolon,
        "';' after alias statement",
        "FSIM-SV-PARSE-284");
    declaration.span = cover(start.span, previous().span);
    if (declaration.terminals.size() < 2) {
        error(
            start,
            "FSIM-SV-SEM-238",
            "an alias statement requires at least two lvalue terminals");
        return;
    }
    unit.systemverilog_aliases.push_back(std::move(declaration));
}

void VerilogParser::parse_let_declaration(
    DesignUnit& unit,
    const Token& start)
{
    SystemVerilogLetDeclaration declaration;
    const auto reference_count = implicit_net_references_.size();
    const auto name = expect_identifier("let declaration name");
    declaration.name = name.text;
    if (match(TokenKind::LeftParen)) {
        std::unordered_set<std::string> names;
        while (!at_end() && !at(TokenKind::RightParen)) {
            const auto port_start = current();
            (void)match_keyword("input");
            std::optional<Type> type;
            const bool builtin_type = any_keyword({ "bit", "byte", "shortint", "int", "longint", "integer",
                "logic", "reg", "time", "shortreal", "real", "realtime",
                "string", "chandle" });
            const bool named_type = at(TokenKind::Identifier)
                && (at(TokenKind::Identifier, 1)
                    || at(TokenKind::Scope, 1));
            if (builtin_type || named_type) {
                type = parse_parameter_type();
            }
            const auto port_name = expect_identifier("let formal name");
            SystemVerilogLetPort port;
            port.name = port_name.text;
            port.type = std::move(type);
            if (match(TokenKind::Assign)) {
                port.default_value = parse_expression();
            }
            port.span = cover(port_start.span, previous().span);
            if (!names.insert(port.name).second) {
                error(
                    port_name,
                    "FSIM-SV-SEM-240",
                    "duplicate let formal '" + port.name + "'");
            } else {
                declaration.ports.push_back(std::move(port));
            }
            if (!match(TokenKind::Comma)) {
                break;
            }
        }
        expect(
            TokenKind::RightParen,
            "')' after let formals",
            "FSIM-SV-PARSE-285");
    }
    expect(
        TokenKind::Assign,
        "'=' before let expression",
        "FSIM-SV-PARSE-286");
    declaration.expression = parse_expression();
    expect(
        TokenKind::Semicolon,
        "';' after let declaration",
        "FSIM-SV-PARSE-287");
    declaration.span = cover(start.span, previous().span);

    const bool duplicate = std::ranges::any_of(
                               unit.systemverilog_lets,
                               [&](const SystemVerilogLetDeclaration& existing) {
                                   return existing.name == declaration.name;
                               })
        || std::ranges::any_of(
            unit.functions,
            [&](const FunctionDeclaration& existing) {
                return existing.name == declaration.name;
            });
    if (duplicate) {
        error(
            name,
            "FSIM-SV-SEM-239",
            "duplicate or conflicting let declaration '"
                + declaration.name + "'");
        return;
    }
    // A let formal is referenced only within its let body.
    if (implicit_net_references_.size() > reference_count) {
        std::erase_if(implicit_net_references_, [&](const ImplicitNetReference& reference) {
            return &reference - implicit_net_references_.data()
                    >= static_cast<std::ptrdiff_t>(reference_count)
                && std::ranges::any_of(declaration.ports, [&](const SystemVerilogLetPort& port) {
                       return port.name == reference.name;
                   });
        });
    }
    unit.systemverilog_lets.push_back(std::move(declaration));
}

void VerilogParser::parse_optional_net_type(Type& type)
{
    if (!is_net_type_keyword()) {
        return;
    }
    auto keyword_token = advance();
    // An interconnect net carries the values of the ports it connects
    // (IEEE 1800-2017 6.6.8); it is kept as a wire.
    if (keyword_token.text == "interconnect") {
        keyword_token.text = "wire";
    }
    const bool net_keyword = contains_word(
        { "wire", "tri", "tri0", "tri1", "wand", "triand", "wor",
            "trior", "trireg", "uwire", "supply0", "supply1" },
        keyword_token.text);
    if (net_keyword) {
        // IEEE 1364-2005 6.1.3: vectored and scalared only advise on
        // bit-select access to a vector net.
        (void)(match_keyword("vectored") || match_keyword("scalared"));
    }
    if (net_keyword && (keyword("logic") || keyword("integer"))) {
        // A net's explicit 4-state integral data type (IEEE 1800-2017
        // 6.7.1): `wire logic [7:0]` is `wire [7:0]`, and `wire integer`
        // is a signed 32-bit wire.
        const auto data_type = advance();
        (void)require_standard(
            "an explicit net data type",
            StandardRevision::SystemVerilog2005,
            data_type);
        type.spelling = keyword_token.text;
        type.domain = ValueDomain::Logic4;
        if (data_type.text == "integer") {
            type.is_signed = true;
            type.packed_range = PackedRange { 31, 0, true };
        }
        return;
    }
    if (net_keyword && keyword_token.text != "supply0"
        && keyword_token.text != "supply1"
        && (keyword("shortreal") || keyword("real")
            || keyword("realtime") || keyword("time"))) {
        type.systemverilog_net_type = keyword_token.text;
        parse_optional_net_type(type);
        return;
    }
    type.spelling = keyword_token.text;
    if (apply_systemverilog_integral_type(type, keyword_token.text)) {
        return;
    }
    if (keyword_token.text == "shortreal"
        || keyword_token.text == "real"
        || keyword_token.text == "realtime") {
        type.domain = ValueDomain::Bit2;
        type.is_signed = true;
        type.systemverilog_scalar = keyword_token.text == "shortreal"
            ? SystemVerilogScalarKind::ShortReal
            : keyword_token.text == "real"
            ? SystemVerilogScalarKind::Real
            : SystemVerilogScalarKind::Realtime;
    } else {
        type.domain = ValueDomain::Logic4;
    }
}

void VerilogParser::parse_optional_signedness(Type& type)
{
    if (match_keyword("signed")) {
        type.is_signed = true;
    } else if (match_keyword("unsigned")) {
        type.is_signed = false;
    } else if (at(TokenKind::Identifier)
        && (current().text == "signed" || current().text == "unsigned")) {
        const auto signedness = advance();
        (void)require_standard(
            "net or variable signedness",
            StandardRevision::Verilog2001,
            signedness);
        type.is_signed = signedness.text == "signed";
    }
}

void VerilogParser::parse_optional_range(Type& type)
{
    std::vector<PackedRangeExpression> dimensions;
    std::uint64_t flattened_width = 1U;
    bool concrete = true;
    while (match(TokenKind::LeftBracket)) {
        const auto start = previous();
        auto left_expression = parse_expression();
        expect(TokenKind::Colon, "':' in packed range", "FSIM-SV-PARSE-005");
        auto right_expression = parse_expression();
        expect(TokenKind::RightBracket, "']' after packed range",
            "FSIM-SV-PARSE-006");
        const auto left = simple_verilog_integer_constant(left_expression);
        const auto right = simple_verilog_integer_constant(right_expression);
        if (left && right && concrete) {
            const auto width = PackedRange { *left, *right, *left >= *right }.width();
            constexpr auto maximum_width = static_cast<std::uint64_t>(
                                               std::numeric_limits<std::int64_t>::max())
                + 1U;
            if (width == 0U || flattened_width > maximum_width / width) {
                concrete = false;
            } else {
                flattened_width *= width;
            }
        } else {
            concrete = false;
        }
        dimensions.push_back(PackedRangeExpression {
            std::move(left_expression),
            std::move(right_expression),
            cover(start.span, previous().span),
            std::nullopt });
    }
    if (dimensions.empty()) {
        return;
    }
    if (dimensions.size() == 1U) {
        const auto left = simple_verilog_integer_constant(dimensions.front().left);
        const auto right = simple_verilog_integer_constant(dimensions.front().right);
        if (left && right) {
            type.packed_range = PackedRange { *left, *right, *left >= *right };
        }
        type.packed_range_expression = std::move(dimensions.front());
        return;
    }
    type.systemverilog_packed_dimensions = std::move(dimensions);
    type.packed_range_expression.reset();
    if (concrete) {
        type.packed_range = PackedRange {
            static_cast<std::int64_t>(flattened_width - 1U), 0, true
        };
    } else {
        type.packed_range.reset();
        // Bounds that are not literals flatten to
        // `[extent0 * extent1 * ... - 1 : 0]`, folded at elaboration.
        const auto span = type.systemverilog_packed_dimensions.front().span;
        const auto literal = [&](const std::string_view text) {
            return Expression { ExpressionKind::IntegerLiteral,
                std::string { text }, { }, span };
        };
        std::optional<Expression> product;
        for (const auto& dimension : type.systemverilog_packed_dimensions) {
            Expression descending { ExpressionKind::Binary, ">=",
                { dimension.left, dimension.right }, span };
            Expression down { ExpressionKind::Binary, "-",
                { dimension.left, dimension.right }, span };
            Expression up { ExpressionKind::Binary, "-",
                { dimension.right, dimension.left }, span };
            Expression distance { ExpressionKind::Call, "?:",
                { std::move(descending), std::move(down), std::move(up) },
                span };
            Expression extent { ExpressionKind::Binary, "+",
                { std::move(distance), literal("1") }, span };
            product = product
                ? Expression { ExpressionKind::Binary, "*",
                      { std::move(*product), std::move(extent) }, span }
                : std::move(extent);
        }
        type.packed_range_expression = PackedRangeExpression {
            Expression { ExpressionKind::Binary, "-",
                { std::move(*product), literal("1") }, span },
            literal("0"), span, std::nullopt };
    }
}

bool VerilogParser::parse_optional_container_dimension(Type& type)
{
    if (!match(TokenKind::LeftBracket)) {
        return false;
    }
    const auto start = previous();
    auto element_type = type;
    element_type.systemverilog_container.reset();
    SystemVerilogContainerInfo container;
    if (match(TokenKind::RightBracket)) {
        container.kind = SystemVerilogContainerKind::DynamicArray;
    } else if (
        at(TokenKind::Identifier)
        && current().text == "$") {
        (void)advance();
        container.kind = SystemVerilogContainerKind::Queue;
        if (match(TokenKind::Colon)) {
            container.queue_maximum = parse_expression();
        }
        expect(
            TokenKind::RightBracket,
            "']' after queue dimension",
            "FSIM-SV-PARSE-155");
    } else {
        const auto built_in = keyword("byte") || keyword("shortint")
            || keyword("longint") || keyword("time")
            || keyword("integer") || keyword("int")
            || keyword("logic") || keyword("reg") || keyword("bit")
            || keyword("string")
            || keyword("signed") || keyword("unsigned")
            || at(TokenKind::LeftBracket);
        if (built_in) {
            container.kind = SystemVerilogContainerKind::AssociativeArray;
            container.associative_index_type = std::make_shared<Type>(parse_parameter_type());
            expect(
                TokenKind::RightBracket,
                "']' after associative-array index type",
                "FSIM-SV-PARSE-157");
        } else if (at(TokenKind::Star)) {
            // A wildcard index takes integral values of any width, compared
            // numerically (IEEE 1800-2017 7.8.1); keys are held in 64
            // unsigned bits.
            (void)advance();
            expect(
                TokenKind::RightBracket,
                "']' after associative-array index type",
                "FSIM-SV-PARSE-157");
            container.kind = SystemVerilogContainerKind::AssociativeArray;
            container.associative_index_type = std::make_shared<Type>(
                ValueDomain::Bit2, "bit", PackedRange { 63, 0, true },
                false);
        } else {
            const auto references_before = implicit_net_references_.size();
            auto left = parse_expression();
            if (match(TokenKind::Colon)) {
                auto right = parse_expression();
                expect(
                    TokenKind::RightBracket,
                    "']' after static unpacked range",
                    "FSIM-SV-PARSE-158");
                container.kind = SystemVerilogContainerKind::StaticArray;
                const auto left_value = simple_verilog_integer_constant(left);
                const auto right_value = simple_verilog_integer_constant(right);
                if (left_value && right_value) {
                    container.static_range = PackedRange {
                        *left_value,
                        *right_value,
                        *left_value >= *right_value
                    };
                }
                container.static_range_expressions.push_back(
                    PackedRangeExpression {
                        std::move(left),
                        std::move(right),
                        cover(start.span, previous().span),
                        std::nullopt });
            } else {
                expect(
                    TokenKind::RightBracket,
                    "']' after associative-array index type",
                    "FSIM-SV-PARSE-157");
                if (const auto size = simple_verilog_integer_constant(left);
                    size && *size > 0) {
                    container.kind = SystemVerilogContainerKind::StaticArray;
                    const auto right_value = *size - 1;
                    container.static_range = PackedRange {
                        0, right_value, false
                    };
                    const auto size_span = left.span;
                    container.static_range_expressions.push_back(
                        PackedRangeExpression {
                            Expression {
                                ExpressionKind::IntegerLiteral,
                                "0",
                                { },
                                size_span },
                            Expression {
                                ExpressionKind::IntegerLiteral,
                                std::to_string(right_value),
                                { },
                                size_span },
                            cover(start.span, previous().span),
                            std::nullopt });
                } else if (left.kind == ExpressionKind::Identifier
                    && !unit_value_parameter_names_.contains(left.text)) {
                    // `[key_t]` names an index type, not a net.
                    if (implicit_net_references_.size() > references_before) {
                        implicit_net_references_.resize(references_before);
                    }
                    container.kind = SystemVerilogContainerKind::AssociativeArray;
                    Type index_type {
                        ValueDomain::Unknown,
                        left.text,
                        std::nullopt,
                        false
                    };
                    index_type.named_type = left.text;
                    index_type.named_type_span = left.span;
                    container.associative_index_type = std::make_shared<Type>(std::move(index_type));
                } else if (left.kind != ExpressionKind::IntegerLiteral
                    && left.kind != ExpressionKind::LogicLiteral) {
                    // A constant size expression `[N + 1]` is `[0:N]`
                    // (IEEE 1800-2017 7.4.2); specialization evaluates it.
                    container.kind = SystemVerilogContainerKind::StaticArray;
                    const auto size_span = left.span;
                    container.static_range_expressions.push_back(
                        PackedRangeExpression {
                            Expression {
                                ExpressionKind::IntegerLiteral,
                                "0",
                                { },
                                size_span },
                            Expression {
                                ExpressionKind::Binary,
                                "-",
                                { std::move(left),
                                    Expression {
                                        ExpressionKind::IntegerLiteral,
                                        "1",
                                        { },
                                        size_span } },
                                size_span },
                            cover(start.span, previous().span),
                            std::nullopt });
                } else {
                    error(
                        start,
                        "FSIM-SV-SEM-078",
                        "static unpacked arrays require a left:right range; "
                        "associative arrays require an integral index type");
                    return true;
                }
            }
        }
    }
    if (language_ != Language::SystemVerilog2017
        && container.kind != SystemVerilogContainerKind::StaticArray) {
        error(
            start,
            "FSIM-SV-SEM-077",
            "dynamic arrays, queues, and associative arrays require "
            "SystemVerilog-2017");
    }
    // IEEE 1800-2017 6.7.1: a net array has fixed unpacked dimensions; a
    // dynamic array, queue, or associative array is a variable form.
    const auto net_spelling = type.systemverilog_net_type.empty()
        ? std::string_view { type.spelling }
        : std::string_view { type.systemverilog_net_type };
    if (container.kind != SystemVerilogContainerKind::StaticArray
        && contains_word(
            { "wire", "tri", "tri0", "tri1", "wand", "triand", "wor",
                "trior", "trireg", "uwire", "supply0", "supply1" },
            net_spelling)) {
        error(
            start,
            "FSIM-SV-SEM-398",
            "a net cannot be a dynamic array, queue, or associative array");
        return true;
    }
    // IEEE 1364-2005 4.9: net arrays are legal Verilog-2005 declarations.
    if ((type.domain == ValueDomain::Unknown
            && type.named_type.empty()
            && type.systemverilog_scalar
                == SystemVerilogScalarKind::None)) {
        error(
            start,
            "FSIM-SV-SEM-079",
            "bounded containers require a resolved variable element type");
    }
    const auto next_dimension_is_nonstatic = [&]() {
        if (!at(TokenKind::LeftBracket))
            return false;
        if (at(TokenKind::RightBracket, 1)
            || at(TokenKind::Star, 1)
            || keyword("$", 1)) {
            return true;
        }
        return at(TokenKind::Identifier, 1)
            && at(TokenKind::RightBracket, 2);
    };
    if (at(TokenKind::LeftBracket)) {
        // Multidimensional unpacked arrays are part of Verilog-2001;
        // only the 1995 revision restricts memories to one dimension.
        (void)require_standard("multidimensional unpacked array",
            StandardRevision::Verilog2001, current(),
            "FSIM-VERILOG-SEM-012");
    }
    if (at(TokenKind::LeftBracket)
        && (container.kind
                != SystemVerilogContainerKind::StaticArray
            || next_dimension_is_nonstatic())) {
        (void)parse_optional_container_dimension(element_type);
    } else {
        while (match(TokenKind::LeftBracket)) {
            const auto dimension_start = previous();
            auto left = parse_expression();
            // A C-style size `[N]` is `[0:N-1]` (IEEE 1800-2017 7.4.2).
            if (at(TokenKind::RightBracket)) {
                if (const auto size = simple_verilog_integer_constant(left);
                    size && *size > 0) {
                    advance();
                    const auto size_span = left.span;
                    container.static_range_expressions.push_back(
                        PackedRangeExpression {
                            Expression {
                                ExpressionKind::IntegerLiteral,
                                "0",
                                { },
                                size_span },
                            Expression {
                                ExpressionKind::IntegerLiteral,
                                std::to_string(*size - 1),
                                { },
                                size_span },
                            cover(dimension_start.span, previous().span),
                            std::nullopt });
                    continue;
                }
            }
            expect(
                TokenKind::Colon,
                "':' in a multidimensional static unpacked range",
                "FSIM-SV-PARSE-221");
            auto right = parse_expression();
            expect(
                TokenKind::RightBracket,
                "']' after a multidimensional static unpacked range",
                "FSIM-SV-PARSE-222");
            container.static_range_expressions.push_back(
                PackedRangeExpression {
                    std::move(left),
                    std::move(right),
                    cover(dimension_start.span, previous().span),
                    std::nullopt });
        }
    }
    container.element_types.push_back(std::move(element_type));
    container.span = cover(start.span, previous().span);
    type.systemverilog_container = std::move(container);
    return true;
}

} // namespace fsim::frontend
