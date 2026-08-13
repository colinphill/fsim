// SPDX-License-Identifier: Apache-2.0
std::size_t VerilogParser::structure_assertion_locals(
    SystemVerilogAssertionDeclaration& declaration)
{
    std::unordered_set<std::string> names;
    for (const auto& formal : declaration.formals) {
        names.insert(formal.name);
    }
    std::size_t position { };
    while (position < declaration.body_tokens.size()) {
        const std::span remaining {
            declaration.body_tokens.data() + position,
            declaration.body_tokens.size() - position
        };
        const auto semicolon = find_top_level(remaining, TokenKind::Semicolon);
        if (!semicolon) {
            break;
        }
        const std::span statement { remaining.data(), *semicolon };
        if (statement.empty()) {
            position += *semicolon + 1U;
            continue;
        }
        const auto first_comma = find_top_level(statement, TokenKind::Comma);
        const std::span first_declarator {
            statement.data(), first_comma.value_or(statement.size())
        };
        const auto assignment = find_top_level(first_declarator, TokenKind::Assign);
        const auto prefix_end = assignment.value_or(first_declarator.size());
        const auto name_position = std::find_if(
            first_declarator.rbegin()
                + static_cast<std::ptrdiff_t>(
                    first_declarator.size() - prefix_end),
            first_declarator.rend(),
            [](const Token& token) {
                return token.kind == TokenKind::Identifier;
            });
        if (name_position == first_declarator.rend()) {
            if (assertion_local_type_keyword(statement.front().text)) {
                error(
                    statement.front(),
                    "FSIM-SV-PARSE-295",
                    "an assertion local-variable declaration has no name");
            }
            break;
        }
        const auto name_index = static_cast<std::size_t>(std::distance(
            first_declarator.begin(), name_position.base() - 1));
        if (name_index == 0) {
            if (assertion_local_type_keyword(statement.front().text)) {
                error(
                    statement.front(),
                    "FSIM-SV-PARSE-295",
                    "an assertion local-variable declaration has no name");
            }
            break;
        }
        const std::span type_tokens { statement.data(), name_index };
        if (!assertion_local_type_keyword(statement.front().text)
            && !valid_named_local_type_prefix(type_tokens)) {
            break;
        }

        const std::vector<Token> common_type(
            type_tokens.begin(), type_tokens.end());
        const std::span declarator_tokens {
            statement.data() + name_index,
            statement.size() - name_index
        };
        auto declarators = split_top_level(declarator_tokens, TokenKind::Comma);
        for (auto& tokens : declarators) {
            if (tokens.empty()
                || tokens.front().kind != TokenKind::Identifier) {
                error(
                    statement.front(),
                    "FSIM-SV-PARSE-295",
                    "an assertion local-variable declarator has no name");
                continue;
            }
            SystemVerilogAssertionLocalVariable local;
            local.type_tokens = common_type;
            local.name = tokens.front().text;
            local.name_span = tokens.front().span;
            local.declarator_tokens = tokens;
            const auto initializer = find_top_level(tokens, TokenKind::Assign);
            if (initializer && *initializer + 1U < tokens.size()) {
                local.initializer_tokens.assign(
                    tokens.begin()
                        + static_cast<std::ptrdiff_t>(*initializer + 1U),
                    tokens.end());
            }
            local.span = span_from(tokens.front(), tokens.back());
            if (!names.insert(local.name).second) {
                error(
                    tokens.front(),
                    "FSIM-SV-SEM-195",
                    "assertion local variable '" + local.name
                        + "' conflicts with a formal or earlier local");
            } else {
                declaration.local_variables.push_back(std::move(local));
            }
        }
        position += *semicolon + 1U;
    }
    return position;
}
