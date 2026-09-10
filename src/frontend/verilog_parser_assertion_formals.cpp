// SPDX-License-Identifier: Apache-2.0

#include "verilog_parser_internal.hpp"
#include "verilog_parser_assertion_support.hpp"

#include <charconv>
#include <unordered_set>

namespace fsim::frontend {

void VerilogParser::structure_assertion_formals(
    SystemVerilogAssertionDeclaration& declaration)
{
    if (declaration.header_tokens.empty()) {
        return;
    }
    const auto& header = declaration.header_tokens;
    if (header.size() < 2
        || header.front().kind != TokenKind::LeftParen
        || header.back().kind != TokenKind::RightParen) {
        error(
            header.front(),
            "FSIM-SV-PARSE-293",
            "an assertion declaration header must be one parenthesized "
            "formal list");
        return;
    }
    int parentheses { };
    bool outer_closed_early { };
    for (std::size_t index = 0; index < header.size(); ++index) {
        if (header[index].kind == TokenKind::LeftParen) {
            ++parentheses;
        } else if (header[index].kind == TokenKind::RightParen) {
            --parentheses;
            if (parentheses == 0 && index + 1U != header.size()) {
                outer_closed_early = true;
            }
        }
        if (parentheses < 0) {
            outer_closed_early = true;
        }
    }
    if (parentheses != 0 || outer_closed_early) {
        error(
            header.front(),
            "FSIM-SV-PARSE-293",
            "an assertion formal list has unbalanced delimiters");
        return;
    }

    const std::span inner { header.data() + 1U, header.size() - 2U };
    if (inner.empty()) {
        return;
    }
    auto items = split_top_level(inner, TokenKind::Comma);
    std::unordered_set<std::string> names;
    for (auto& item : items) {
        if (item.empty()) {
            error(
                header.front(),
                "FSIM-SV-PARSE-294",
                "an assertion formal argument is empty");
            continue;
        }
        const auto assignment = find_top_level(item, TokenKind::Assign);
        const auto prefix_end = assignment.value_or(item.size());
        const auto name_position = std::find_if(
            item.rbegin() + static_cast<std::ptrdiff_t>(item.size() - prefix_end),
            item.rend(),
            [](const Token& token) {
                return token.kind == TokenKind::Identifier;
            });
        if (name_position == item.rend()) {
            error(
                item.front(),
                "FSIM-SV-PARSE-294",
                "an assertion formal argument has no name");
            continue;
        }
        const auto name_index = static_cast<std::size_t>(
            std::distance(item.begin(), name_position.base() - 1));
        std::size_t type_start { };
        SystemVerilogAssertionFormal formal;
        if (item[type_start].text == "input") {
            formal.direction = PortDirection::Input;
            ++type_start;
        } else if (item[type_start].text == "output") {
            formal.direction = PortDirection::Output;
            ++type_start;
        } else if (item[type_start].text == "inout") {
            formal.direction = PortDirection::Inout;
            ++type_start;
        } else if (item[type_start].text == "ref") {
            formal.direction = PortDirection::Ref;
            ++type_start;
        } else if (item[type_start].text == "local") {
            formal.local = true;
            ++type_start;
        }
        if (type_start > name_index) {
            error(
                item.front(),
                "FSIM-SV-PARSE-294",
                "an assertion formal argument has no declared name");
            continue;
        }
        formal.type_tokens.assign(
            item.begin() + static_cast<std::ptrdiff_t>(type_start),
            item.begin() + static_cast<std::ptrdiff_t>(name_index));
        if (formal.type_tokens.empty()
            || formal.type_tokens.front().text == "untyped") {
            formal.kind = SystemVerilogAssertionFormalKind::Untyped;
        } else if (formal.type_tokens.front().text == "sequence") {
            formal.kind = SystemVerilogAssertionFormalKind::Sequence;
        } else if (formal.type_tokens.front().text == "property") {
            formal.kind = SystemVerilogAssertionFormalKind::Property;
        } else {
            formal.kind = SystemVerilogAssertionFormalKind::Value;
        }
        formal.name = item[name_index].text;
        formal.name_span = item[name_index].span;
        if (assignment && *assignment + 1U < item.size()) {
            formal.default_tokens.assign(
                item.begin() + static_cast<std::ptrdiff_t>(*assignment + 1U),
                item.end());
        }
        formal.span = span_from(item.front(), item.back());
        if (!names.insert(formal.name).second) {
            error(
                item[name_index],
                "FSIM-SV-SEM-194",
                "duplicate assertion formal argument '" + formal.name + "'");
        } else {
            declaration.formals.push_back(std::move(formal));
        }
    }
}

} // namespace fsim::frontend
