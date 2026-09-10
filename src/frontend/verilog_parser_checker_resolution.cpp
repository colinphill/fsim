// SPDX-License-Identifier: Apache-2.0

#include "verilog_parser_internal.hpp"

#include <algorithm>
#include <iterator>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::frontend {

void VerilogParser::instantiate_checkers(DesignUnit& unit)
{
    using TokenMap = std::unordered_map<std::string, std::vector<Token>>;
    const auto substitute_tokens = [](
                                       const std::span<const Token> source,
                                       const TokenMap& actuals,
                                       const std::unordered_map<std::string, std::string>& names,
                                       const std::unordered_set<std::string>& shadowed) {
        std::vector<Token> result;
        for (const auto& token : source) {
            if (token.kind != TokenKind::Identifier
                || shadowed.contains(token.text)) {
                result.push_back(token);
                continue;
            }
            if (const auto renamed = names.find(token.text);
                renamed != names.end()) {
                auto replacement = token;
                replacement.text = renamed->second;
                result.push_back(std::move(replacement));
                continue;
            }
            const auto actual = actuals.find(token.text);
            if (actual == actuals.end()) {
                result.push_back(token);
                continue;
            }
            const bool parenthesize = actual->second.size() > 1U;
            if (parenthesize) {
                result.push_back(Token {
                    TokenKind::LeftParen, "(", token.span,
                    token.expansion_stack });
            }
            result.insert(
                result.end(), actual->second.begin(), actual->second.end());
            if (parenthesize) {
                result.push_back(Token {
                    TokenKind::RightParen, ")", token.span,
                    token.expansion_stack });
            }
        }
        return result;
    };

    std::vector<SystemVerilogAssertionDeclaration> declarations;
    std::vector<SystemVerilogConcurrentAssertion> assertions;
    for (const auto& instance : unit.systemverilog_checker_instances) {
        const auto checker = std::ranges::find_if(
            unit.systemverilog_assertion_declarations,
            [&](const SystemVerilogAssertionDeclaration& declaration) {
                return declaration.kind
                    == SystemVerilogAssertionDeclarationKind::Checker
                    && declaration.name == instance.declaration_name;
            });
        if (checker == unit.systemverilog_assertion_declarations.end()) {
            error(
                Token {
                    TokenKind::Identifier,
                    instance.declaration_name,
                    instance.span,
                    { } },
                "FSIM-SV-SEM-253",
                "checker instance '" + instance.name
                    + "' has no visible declaration");
            continue;
        }

        TokenMap actuals;
        std::unordered_set<std::string> connected;
        std::size_t ordered_index { };
        bool wildcard { };
        for (const auto& connection : instance.connections) {
            if (connection.formal_name == "*") {
                wildcard = true;
                continue;
            }
            const SystemVerilogAssertionFormal* formal { };
            if (connection.formal_name.empty()) {
                if (ordered_index >= checker->formals.size()) {
                    error(
                        Token {
                            TokenKind::Identifier,
                            instance.name,
                            connection.span,
                            { } },
                        "FSIM-SV-SEM-255",
                        "checker instance '" + instance.name
                            + "' has too many ordered connections");
                    continue;
                }
                formal = &checker->formals[ordered_index++];
            } else {
                const auto found = std::ranges::find(
                    checker->formals,
                    connection.formal_name,
                    &SystemVerilogAssertionFormal::name);
                if (found == checker->formals.end()) {
                    error(
                        Token {
                            TokenKind::Identifier,
                            connection.formal_name,
                            connection.span,
                            { } },
                        "FSIM-SV-SEM-253",
                        "checker declaration '" + checker->name
                            + "' has no formal '" + connection.formal_name
                            + "'");
                    continue;
                }
                formal = &*found;
            }
            if (!connected.insert(formal->name).second) {
                error(
                    Token {
                        TokenKind::Identifier,
                        formal->name,
                        connection.span,
                        { } },
                    "FSIM-SV-SEM-254",
                    "checker formal '" + formal->name
                        + "' is connected more than once");
                continue;
            }
            if (!connection.actual_tokens.empty()) {
                actuals.emplace(formal->name, connection.actual_tokens);
            }
        }
        for (const auto& formal : checker->formals) {
            if (actuals.contains(formal.name)) {
                continue;
            }
            if (wildcard && !connected.contains(formal.name)) {
                actuals.emplace(
                    formal.name,
                    std::vector<Token> { Token {
                        TokenKind::Identifier,
                        formal.name,
                        instance.span,
                        { } } });
            } else if (!formal.default_tokens.empty()) {
                actuals.emplace(
                    formal.name,
                    substitute_tokens(
                        formal.default_tokens, actuals, { }, { }));
            } else {
                error(
                    Token {
                        TokenKind::Identifier,
                        formal.name,
                        instance.span,
                        { } },
                    "FSIM-SV-SEM-255",
                    "checker instance '" + instance.name
                        + "' leaves required formal '" + formal.name
                        + "' unconnected");
            }
        }

        std::unordered_map<std::string, std::string> declaration_names;
        for (const auto& declaration : checker->checker_declarations) {
            declaration_names.emplace(
                declaration.name,
                instance.name + "$" + declaration.name);
        }
        for (const auto& source : checker->checker_declarations) {
            auto declaration = source;
            declaration.name = declaration_names.at(source.name);
            std::unordered_set<std::string> shadowed;
            for (const auto& formal : declaration.formals) {
                shadowed.insert(formal.name);
            }
            for (const auto& local : declaration.local_variables) {
                shadowed.insert(local.name);
            }
            declaration.header_tokens = substitute_tokens(
                declaration.header_tokens, actuals, declaration_names,
                shadowed);
            declaration.body_tokens = substitute_tokens(
                declaration.body_tokens, actuals, declaration_names,
                shadowed);
            for (auto& formal : declaration.formals) {
                formal.default_tokens = substitute_tokens(
                    formal.default_tokens, actuals, declaration_names,
                    shadowed);
            }
            for (auto& local : declaration.local_variables) {
                local.initializer_tokens = substitute_tokens(
                    local.initializer_tokens, actuals, declaration_names,
                    shadowed);
            }
            if (declaration.clock) {
                declaration.clock->event_tokens = substitute_tokens(
                    declaration.clock->event_tokens, actuals,
                    declaration_names, shadowed);
            }
            if (declaration.disable) {
                declaration.disable->condition_tokens = substitute_tokens(
                    declaration.disable->condition_tokens, actuals,
                    declaration_names, shadowed);
            }
            declaration.expression_tokens = substitute_tokens(
                declaration.expression_tokens, actuals, declaration_names,
                shadowed);
            declaration.references.clear();
            declaration.sequence_expression.reset();
            declaration.property_expression.reset();
            declaration.sequence_endpoints.clear();
            structure_assertion_endpoints(declaration);
            structure_sequence_expression(declaration);
            structure_property_expression(declaration);
            declarations.push_back(std::move(declaration));
        }
        for (std::size_t index = 0;
            index < checker->checker_assertions.size(); ++index) {
            auto assertion = checker->checker_assertions[index];
            assertion.property_tokens = substitute_tokens(
                assertion.property_tokens, actuals, declaration_names, { });
            assertion.pass_action_tokens = substitute_tokens(
                assertion.pass_action_tokens, actuals, declaration_names, { });
            assertion.failure_action_tokens = substitute_tokens(
                assertion.failure_action_tokens, actuals, declaration_names,
                { });
            assertion.label = instance.name + "."
                + (assertion.label.empty()
                        ? "$assertion" + std::to_string(index)
                        : assertion.label);
            if (assertion.inline_sequence) {
                assertion.inline_sequence->expression_tokens
                    = substitute_tokens(
                        assertion.inline_sequence->expression_tokens,
                        actuals, declaration_names, { });
                assertion.inline_sequence->sequence_expression.reset();
                assertion.inline_sequence->sequence_endpoints.clear();
                structure_assertion_endpoints(*assertion.inline_sequence);
                structure_sequence_expression(*assertion.inline_sequence);
            }
            assertions.push_back(std::move(assertion));
        }
    }
    unit.systemverilog_assertion_declarations.insert(
        unit.systemverilog_assertion_declarations.end(),
        std::make_move_iterator(declarations.begin()),
        std::make_move_iterator(declarations.end()));
    unit.systemverilog_concurrent_assertions.insert(
        unit.systemverilog_concurrent_assertions.end(),
        std::make_move_iterator(assertions.begin()),
        std::make_move_iterator(assertions.end()));
}

} // namespace fsim::frontend
