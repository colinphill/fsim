// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

namespace fsim::frontend {

std::string VerilogParser::parse_systemverilog_hierarchical_name(
    const std::string_view description,
    const bool allow_indices)
{
    const auto first = expect_identifier(description);
    std::string result = first.text;
    for (;;) {
        if (allow_indices && match(TokenKind::LeftBracket)) {
            result += '[';
            std::size_t depth = 1;
            while (!at_end() && depth != 0U) {
                if (at(TokenKind::LeftBracket)) {
                    ++depth;
                    result += advance().text;
                } else if (at(TokenKind::RightBracket)) {
                    --depth;
                    if (depth != 0U) {
                        result += advance().text;
                    } else {
                        (void)advance();
                    }
                } else {
                    result += advance().text;
                }
            }
            if (depth != 0U) {
                error(
                    first,
                    "FSIM-SV-PARSE-354",
                    "unterminated index in " + std::string { description });
            }
            result += ']';
            continue;
        }
        if (!match(TokenKind::Dot)) {
            break;
        }
        result += '.';
        result += expect_identifier(description).text;
    }
    return result;
}

SystemVerilogBindDirective VerilogParser::parse_systemverilog_bind(
    const Token& start)
{
    SystemVerilogBindDirective directive;
    directive.target = parse_systemverilog_hierarchical_name(
        "bind target", true);
    directive.instances = parse_instances();
    directive.span = span_from(start, previous());
    return directive;
}

DesignUnit VerilogParser::make_systemverilog_bind_unit(
    SystemVerilogBindDirective directive)
{
    DesignUnit unit;
    unit.kind = UnitKind::SystemVerilogBind;
    unit.language = language_;
    unit.standard_revision = standard_revision_;
    unit.verilog_compatibility_profile = compatibility_profile_;
    unit.name = "$bind$" + std::to_string(next_bind_unit_++);
    unit.systemverilog_imports = compilation_unit_imports_;
    unit.default_nettype = current_default_nettype_;
    unit.is_cell = current_cell_define_;
    unit.span = directive.span;
    unit.systemverilog_binds.push_back(std::move(directive));
    return unit;
}

DesignUnit VerilogParser::parse_systemverilog_configuration(
    const Token& start)
{
    DesignUnit unit;
    unit.kind = UnitKind::SystemVerilogConfiguration;
    unit.language = language_;
    unit.standard_revision = standard_revision_;
    unit.verilog_compatibility_profile = compatibility_profile_;
    unit.systemverilog_imports = compilation_unit_imports_;
    unit.default_nettype = current_default_nettype_;
    unit.is_cell = current_cell_define_;
    unit.name = expect_identifier("configuration name").text;
    expect(
        TokenKind::Semicolon,
        "';' after configuration name",
        "FSIM-SV-PARSE-355");

    SystemVerilogConfigurationDeclaration configuration;
    bool saw_design = false;
    bool saw_default = false;
    while (!at_end() && !keyword("endconfig")) {
        if (match_keyword("design")) {
            const auto declaration = previous();
            if (saw_design) {
                error(
                    declaration,
                    "FSIM-SV-SEM-233",
                    "a configuration contains exactly one design statement");
            }
            saw_design = true;
            do {
                const auto design_start = current();
                const auto selected = parse_systemverilog_hierarchical_name(
                    "configured top cell", false);
                SystemVerilogConfigurationDeclaration::Design design;
                if (const auto dot = selected.find('.');
                    dot != std::string::npos) {
                    design.library = selected.substr(0, dot);
                    design.cell = selected.substr(dot + 1U);
                } else {
                    design.cell = selected;
                }
                design.span = span_from(design_start, previous());
                configuration.designs.push_back(std::move(design));
            } while (!at_end() && !at(TokenKind::Semicolon));
            expect(
                TokenKind::Semicolon,
                "';' after configuration design statement",
                "FSIM-SV-PARSE-356");
            continue;
        }
        if (match_keyword("default")) {
            const auto declaration = previous();
            if (saw_default) {
                error(
                    declaration,
                    "FSIM-SV-SEM-234",
                    "duplicate default liblist in configuration");
            }
            saw_default = true;
            expect_keyword("liblist", false, "FSIM-SV-PARSE-357");
            while (!at_end() && !at(TokenKind::Semicolon)) {
                configuration.default_liblist.push_back(
                    expect_identifier("default configuration library").text);
            }
            expect(
                TokenKind::Semicolon,
                "';' after default liblist",
                "FSIM-SV-PARSE-358");
            continue;
        }
        const bool instance_rule = match_keyword("instance");
        const bool cell_rule = !instance_rule && match_keyword("cell");
        if (!instance_rule && !cell_rule) {
            const auto unexpected = advance();
            error(
                unexpected,
                "FSIM-SV-PARSE-359",
                "expected design, default, instance, cell, or endconfig");
            skip_to_semicolon();
            continue;
        }

        const auto rule_start = previous();
        SystemVerilogConfigurationRule rule;
        rule.kind = instance_rule
            ? SystemVerilogConfigurationRuleKind::Instance
            : SystemVerilogConfigurationRuleKind::Cell;
        rule.selector = parse_systemverilog_hierarchical_name(
            instance_rule ? "configuration instance path"
                          : "configuration cell name",
            instance_rule);
        if (match_keyword("use")) {
            rule.selection = SystemVerilogConfigurationSelectionKind::Use;
            const auto selected = parse_systemverilog_hierarchical_name(
                "configuration use cell", false);
            if (const auto dot = selected.find('.');
                dot != std::string::npos) {
                rule.use_library = selected.substr(0, dot);
                rule.use_cell = selected.substr(dot + 1U);
            } else {
                rule.use_cell = selected;
            }
            if (match(TokenKind::Colon)) {
                expect_keyword("config", false, "FSIM-SV-PARSE-360");
                rule.use_configuration = true;
            }
        } else if (match_keyword("liblist")) {
            rule.selection = SystemVerilogConfigurationSelectionKind::Liblist;
            while (!at_end() && !at(TokenKind::Semicolon)) {
                rule.liblist.push_back(
                    expect_identifier("configuration rule library").text);
            }
        } else {
            error(
                current(),
                "FSIM-SV-PARSE-361",
                "configuration rule requires use or liblist");
        }
        expect(
            TokenKind::Semicolon,
            "';' after configuration rule",
            "FSIM-SV-PARSE-362");
        rule.span = span_from(rule_start, previous());
        configuration.rules.push_back(std::move(rule));
    }
    const auto end = expect_keyword(
        "endconfig", false, "FSIM-SV-PARSE-363");
    if (match(TokenKind::Colon)) {
        const auto end_name = expect_identifier(
            "configuration name after endconfig");
        if (end_name.text != unit.name) {
            error(
                end_name,
                "FSIM-SV-SEM-235",
                "configuration end name does not match '" + unit.name + "'");
        }
    }
    if (!saw_design || configuration.designs.empty()) {
        error(
            start,
            "FSIM-SV-SEM-236",
            "configuration '" + unit.name + "' requires a design statement");
    }
    configuration.span = span_from(start, previous());
    unit.systemverilog_configuration = std::move(configuration);
    unit.span = span_from(start, previous());
    (void)end;
    return unit;
}

} // namespace fsim::frontend
