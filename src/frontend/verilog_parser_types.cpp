// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

#include <bit>

namespace fsim::frontend {

Type VerilogParser::parse_systemverilog_aggregate_type()
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
        const auto member_start = current();
        Type member_type;
        if (keyword("struct") || keyword("union")) {
            member_type = parse_systemverilog_aggregate_type();
        } else if (
            keyword("logic") || keyword("reg") || keyword("bit")
            || keyword("byte") || keyword("shortint")
            || keyword("int") || keyword("longint")
            || keyword("integer") || keyword("time")) {
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
            if (match(TokenKind::Semicolon)) {
                continue;
            }
            break;
        }
        if (packed
            && (member_type.packed_aggregate
                    == PackedAggregateKind::UnpackedStruct
                || member_type.packed_aggregate
                    == PackedAggregateKind::UnpackedUnion)) {
            error(
                member_start,
                "FSIM-SV-UNSUPPORTED-028",
                "a packed aggregate cannot contain an unpacked aggregate member");
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
                        != PackedAggregateKind::None) {
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
                                "all members of an ordinary packed union must "
                                "have the same width");
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
        type.domain = ValueDomain::Integer;
        type.spelling = "int";
        type.is_signed = true;
    } else if (at(TokenKind::Identifier)) {
        type = parse_named_type();
    } else {
        type.domain = ValueDomain::Integer;
        type.spelling = "int";
        type.is_signed = true;
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
        Expression value;
        if (match(TokenKind::Assign)) {
            value = parse_expression();
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
        type.enumeration_literals.push_back(literal.text);
        type.systemverilog_enumeration_values.push_back(value);
        if (retained_literals != nullptr) {
            retained_literals->push_back({ literal.text,
                std::move(value),
                cover(literal.span, previous().span) });
        }
        previous_literal = literal.text;
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
    if (keyword("struct") || keyword("union")) {
        const bool is_union = match_keyword("union");
        if (!is_union) {
            (void)match_keyword("struct");
        }
        bool tagged = is_union && match_keyword("tagged");
        const bool packed = match_keyword("packed");
        if (is_union && !tagged && match_keyword("tagged")) {
            tagged = true;
        }
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
            return;
        }
        (void)advance();
        if (at(TokenKind::RightBrace)) {
            error(
                current(),
                "FSIM-SV-PARSE-089",
                "bounded packed aggregates require at least one member");
        }
        std::unordered_set<std::string> member_names;
        std::optional<std::uint64_t> ordinary_union_width;
        while (!at_end() && !at(TokenKind::RightBrace)) {
            const auto member_start = current();
            Type member_type;
            if (keyword("struct") || keyword("union")) {
                member_type = parse_systemverilog_aggregate_type();
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
                    "bounded aggregate members require an executable built-in or "
                    "visible named type");
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
                const auto member = expect_identifier("packed aggregate member name");
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
                    member_initializer = parse_expression();
                }
                if (!member_names.insert(member.text).second) {
                    error(
                        member,
                        "FSIM-SV-SEM-025",
                        "duplicate packed aggregate member '"
                            + member.text + "'");
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
                            != PackedAggregateKind::None) {
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
                "';' after packed aggregate member declaration",
                "FSIM-SV-PARSE-088");
        }
        expect(
            TokenKind::RightBrace,
            "'}' after packed aggregate members",
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
            } else {
                if (*width
                    > std::numeric_limits<std::uint64_t>::max()
                        - total_width) {
                    concrete = false;
                    break;
                }
                total_width += *width;
            }
        }
        const auto tag_width = tagged && !type.packed_members.empty()
            ? std::max<std::uint64_t>(
                  1U,
                  static_cast<std::uint64_t>(
                      std::bit_width(type.packed_members.size() - 1U)))
            : 0U;
        if (concrete
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
                static_cast<std::int64_t>(
                    total_width + tag_width - 1U),
                0,
                true
            };
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
        || keyword("chandle") || keyword("process")
        || keyword("logic") || keyword("reg") || keyword("bit")
        || keyword("byte") || keyword("shortint") || keyword("int")
        || keyword("longint") || keyword("integer")) {
        type = parse_parameter_type();
    } else if (is_named_type_reference_start()) {
        type = parse_named_type();
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
    unit.type_aliases.push_back({ name.text,
        std::move(type),
        cover(start.span, previous().span),
        std::move(enum_literals),
        TypeDeclarationKind::SystemVerilogTypedef,
        { } });
    for (auto& [parameter, parameter_name] :
        enum_parameters) {
        add_parameter(
            unit, std::move(parameter), parameter_name);
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
        { }
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
    unit.systemverilog_lets.push_back(std::move(declaration));
}

void VerilogParser::parse_optional_net_type(Type& type)
{
    if (!is_net_type_keyword()) {
        return;
    }
    const auto keyword_token = advance();
    if (contains_word(
            { "wire", "tri", "tri0", "tri1", "wand", "triand", "wor",
                "trior", "trireg", "uwire" },
            keyword_token.text)
        && (keyword("shortreal") || keyword("real")
            || keyword("realtime") || keyword("time"))) {
        type.systemverilog_net_type = keyword_token.text;
        parse_optional_net_type(type);
        return;
    }
    type.spelling = keyword_token.text;
    if (keyword_token.text == "bit") {
        type.domain = ValueDomain::Bit2;
    } else if (
        keyword_token.text == "byte"
        || keyword_token.text == "shortint"
        || keyword_token.text == "int"
        || keyword_token.text == "longint"
        || keyword_token.text == "integer") {
        type.domain = keyword_token.text == "integer"
            ? ValueDomain::Logic4
            : ValueDomain::Bit2;
        type.is_signed = true;
        const auto width = keyword_token.text == "byte"
            ? std::int64_t { 8 }
            : keyword_token.text == "shortint"
            ? std::int64_t { 16 }
            : keyword_token.text == "longint"
            ? std::int64_t { 64 }
            : std::int64_t { 32 };
        type.packed_range = PackedRange {
            width - 1, 0, true
        };
    } else if (keyword_token.text == "shortreal"
        || keyword_token.text == "real"
        || keyword_token.text == "realtime") {
        type.domain = ValueDomain::Bit2;
        type.is_signed = true;
        type.systemverilog_scalar = keyword_token.text == "shortreal"
            ? SystemVerilogScalarKind::ShortReal
            : keyword_token.text == "real"
            ? SystemVerilogScalarKind::Real
            : SystemVerilogScalarKind::Realtime;
    } else if (keyword_token.text == "time") {
        type.domain = ValueDomain::Logic4;
        type.is_signed = false;
        type.packed_range = PackedRange { 63, 0, true };
        type.systemverilog_scalar = SystemVerilogScalarKind::Time;
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
            error(
                current(),
                "FSIM-SV-SEM-078",
                "wildcard associative-array indices are not supported");
            (void)advance();
            expect(
                TokenKind::RightBracket,
                "']' after associative-array index type",
                "FSIM-SV-PARSE-157");
            return true;
        } else {
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
                } else if (left.kind == ExpressionKind::Identifier) {
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
    if ((language_ == Language::Verilog2005
            && type.spelling == "wire")
        || (type.domain == ValueDomain::Unknown
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
    if (language_ == Language::Verilog2005
        && at(TokenKind::LeftBracket)) {
        error(
            current(),
            "FSIM-VERILOG-SEM-012",
            "Verilog-2005 memories support exactly one unpacked dimension");
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
