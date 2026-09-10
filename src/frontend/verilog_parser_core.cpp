// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

namespace fsim::frontend {

namespace {

[[nodiscard]] std::optional<StandardRevision> structural_word_standard(
    const std::string_view word) {
  if (word == "config" || word == "generate" || word == "genvar") {
    return StandardRevision::Verilog2001;
  }
  if (contains_word(
          {"alias", "assert", "assume", "bind", "class", "clocking",
           "constraint", "cover", "covergroup", "export", "extends",
           "import", "interface", "modport", "package", "program",
           "property", "restrict", "sequence", "typedef", "virtual"},
          word)) {
    return StandardRevision::SystemVerilog2005;
  }
  if (word == "checker" || word == "let") {
    return StandardRevision::SystemVerilog2009;
  }
  if (word == "implements") {
    return StandardRevision::SystemVerilog2012;
  }
  return std::nullopt;
}

[[nodiscard]] bool profile_has(
    const std::string_view profile,
    const std::string_view name) noexcept {
  std::size_t offset = 0;
  while (offset <= profile.size()) {
    const auto separator = profile.find(',', offset);
    const auto end = separator == std::string_view::npos
        ? profile.size()
        : separator;
    if (profile.substr(offset, end - offset) == name) {
      return true;
    }
    if (separator == std::string_view::npos) {
      break;
    }
    offset = separator + 1;
  }
  return false;
}

[[nodiscard]] KeywordSet compatibility_keyword_set(
    const StandardRevision revision,
    const std::string_view profile) {
  if (revision == StandardRevision::Verilog2001NoConfig
      && profile_has(profile, "configuration")) {
    return KeywordSet::Verilog2001;
  }
  if (!profile_has(profile, "keyword-profile")) {
    return keyword_set_for_standard_revision(revision);
  }
  switch (revision) {
  case StandardRevision::Verilog2005:
    return KeywordSet::Verilog2001;
  case StandardRevision::SystemVerilog2009:
  case StandardRevision::SystemVerilog2012:
  case StandardRevision::SystemVerilog2017:
    return KeywordSet::SystemVerilog2005;
  default:
    return keyword_set_for_standard_revision(revision);
  }
}

} // namespace

[[nodiscard]] std::optional<StandardRevision>
verilog_system_service_standard(const std::string_view name)
{
    if (contains_word(
            { "$fflush", "$ferror", "$fgetc", "$fgets", "$fread",
                "$fscanf", "$fseek", "$ftell", "$rewind", "$sscanf",
                "$q_add", "$q_exam", "$q_full", "$q_initialize",
                "$q_remove", "$sformat", "$signed", "$swrite", "$swriteb",
                "$swriteh", "$swriteo", "$test$plusargs", "$ungetc",
                "$unsigned", "$value$plusargs", "$writememb", "$writememh" },
            name)) {
        return StandardRevision::Verilog2001;
    }
    if (contains_word(
            { "$acos", "$acosh", "$asin", "$asinh", "$atan", "$atan2",
                "$atanh", "$ceil", "$clog2", "$cos", "$cosh", "$exp",
                "$floor", "$hypot", "$ln", "$log10", "$pow", "$sin",
                "$sinh", "$sqrt", "$tan", "$tanh" },
            name)) {
        return StandardRevision::Verilog2005;
    }
    if (contains_word(
            { "$assertcontrol", "$assertkill", "$assertoff", "$asserton",
                "$bits", "$bitstoshortreal", "$cast", "$changed",
                "$countbits", "$countones", "$dimensions", "$error", "$exit",
                "$coverage_control", "$coverage_get", "$coverage_get_max",
                "$coverage_merge", "$coverage_save", "$fatal", "$fell", "$get_coverage",
                "$get_inst_coverage",
                "$high", "$increment", "$info", "$isunknown", "$isunbounded",
                "$left", "$low", "$onehot", "$onehot0", "$past", "$right",
                "$rose", "$sampled", "$sformatf", "$shortrealtobits", "$size",
                "$srandom", "$stable", "$system", "$typename",
                "$unpacked_dimensions", "$urandom", "$urandom_range",
                "$warning" },
            name)) {
        return StandardRevision::SystemVerilog2005;
    }
    if (contains_word(
            { "$assertfailoff", "$assertfailon", "$assertnonvacuouson",
                "$assertpassoff", "$assertpasson", "$assertvacuousoff",
                "$changed_gclk", "$changing_gclk", "$falling_gclk",
                "$fell_gclk", "$future_gclk", "$past_gclk", "$rising_gclk",
                "$rose_gclk", "$stable_gclk", "$steady_gclk" },
            name)) {
        return StandardRevision::SystemVerilog2009;
    }
    return std::nullopt;
}

VerilogParser::VerilogParser(LexResult lexed, bool system_verilog)
    : VerilogParser(
          std::move(lexed),
          system_verilog ? StandardRevision::SystemVerilog2017
                         : StandardRevision::Verilog2005) {}

VerilogParser::VerilogParser(
    LexResult lexed,
    const StandardRevision standard_revision)
    : VerilogParser(
          std::move(lexed), standard_revision, "none") {}

VerilogParser::VerilogParser(
    LexResult lexed,
    const StandardRevision standard_revision,
    std::string compatibility_profile)
    : ParserBase(std::move(lexed.tokens),
                 std::move(lexed.diagnostics)),
      language_(language_for_standard_revision(standard_revision)),
      standard_revision_(standard_revision),
      compatibility_profile_(std::move(compatibility_profile)),
      keyword_set_(compatibility_keyword_set(
          standard_revision, compatibility_profile_)) {}

ParseResult VerilogParser::run() {
    ParsedDesign design;
    while (!at_end()) {
        if (verilog_attribute_instance_start()) {
            parse_verilog_attribute_instances();
        } else if (match_keyword("checker")) {
            compilation_unit_has_design_item_ = true;
            const auto checker_start = previous();
            (void)require_standard(
                "a checker declaration",
                StandardRevision::SystemVerilog2009,
                checker_start,
                "FSIM-SV-PARSE-348");
            auto declaration = parse_assertion_declaration(
                checker_start,
                SystemVerilogAssertionDeclarationKind::Checker);
            if (std::ranges::any_of(
                    compilation_unit_checkers_,
                    [&](const auto& existing) {
                        return !declaration.name.empty()
                            && existing.name == declaration.name;
                    })) {
                error(
                    checker_start,
                    "FSIM-SV-SEM-192",
                    "duplicate compilation-unit checker declaration '"
                        + declaration.name + "'");
            } else {
                compilation_unit_checkers_.push_back(std::move(declaration));
            }
        } else if (time_declaration_start()) {
            const auto declaration = advance();
            parse_time_declaration(nullptr, declaration);
        } else if (
            match_keyword("module")
            || match_keyword("macromodule")) {
            compilation_unit_has_design_item_ = true;
            design.units.push_back(parse_module(previous()));
        } else if (match_keyword("extern")) {
            compilation_unit_has_design_item_ = true;
            const auto extern_token = previous();
            UnitKind kind { UnitKind::VerilogModule };
            Token declaration;
            if (match_keyword("module") || match_keyword("macromodule")) {
                declaration = previous();
            } else if (match_keyword("interface")) {
                kind = UnitKind::SystemVerilogInterface;
                declaration = previous();
            } else if (match_keyword("program")) {
                kind = UnitKind::SystemVerilogProgram;
                declaration = previous();
            } else {
                error(
                    extern_token,
                    "FSIM-SV-PARSE-353",
                    "extern must introduce a module, interface, or program declaration");
                skip_to_semicolon();
                continue;
            }
            auto unit = parse_module(declaration, kind, true);
            unit.span = cover(extern_token.span, unit.span);
            design.units.push_back(std::move(unit));
        } else if (match_keyword("config")) {
            compilation_unit_has_design_item_ = true;
            const auto config_token = previous();
            if (standard_revision_ == StandardRevision::Verilog2001NoConfig
                && !compatibility_enabled("configuration")) {
                error(
                    config_token,
                    "FSIM-SV-PARSE-348",
                    "configuration declarations are disabled by selected verilog-2001-noconfig; select verilog-2001 or verilog-2005");
            } else {
                (void)require_standard(
                    "a configuration declaration",
                    StandardRevision::Verilog2001,
                    config_token,
                    "FSIM-SV-PARSE-348");
            }
            design.units.push_back(
                parse_systemverilog_configuration(config_token));
        } else if (match_keyword("bind")) {
            compilation_unit_has_design_item_ = true;
            (void)require_standard(
                "a bind directive", StandardRevision::SystemVerilog2005,
                previous(), "FSIM-SV-PARSE-348");
            design.units.push_back(make_systemverilog_bind_unit(
                parse_systemverilog_bind(previous())));
        } else if (match_keyword("interface")) {
            compilation_unit_has_design_item_ = true;
            const auto interface_token = previous();
            (void)require_standard(
                "an interface declaration",
                StandardRevision::SystemVerilog2005,
                interface_token,
                "FSIM-SV-PARSE-348");
            if (match_keyword("class")) {
                (void)require_standard(
                    "an interface class",
                    StandardRevision::SystemVerilog2012,
                    previous(),
                    "FSIM-SV-PARSE-348");
                add_class_declaration(
                    design.systemverilog_classes,
                    parse_class(previous(), "$unit", false, true),
                    interface_token);
            } else {
                design.units.push_back(parse_module(
                    interface_token, UnitKind::SystemVerilogInterface));
            }
        } else if (match_keyword("program")) {
            compilation_unit_has_design_item_ = true;
            const auto program_token = previous();
            (void)require_standard(
                "a program declaration",
                StandardRevision::SystemVerilog2005,
                program_token,
                "FSIM-SV-PARSE-348");
            if (at(TokenKind::Semicolon)) {
                parse_anonymous_program(design, program_token);
            } else {
                design.units.push_back(parse_module(
                    program_token, UnitKind::SystemVerilogProgram));
            }
        } else if (
            at(TokenKind::Identifier) && current().text == "program") {
            compilation_unit_has_design_item_ = true;
            const auto program_token = advance();
            (void)require_standard(
                "a program declaration",
                StandardRevision::SystemVerilog2005,
                program_token,
                "FSIM-SV-PARSE-348");
            skip_to_semicolon();
        } else if (match_keyword("virtual")) {
            compilation_unit_has_design_item_ = true;
            const auto qualifier = previous();
            if (match_keyword("class")) {
                add_class_declaration(
                    design.systemverilog_classes,
                    parse_class(previous(), "$unit", true),
                    qualifier);
            } else {
                error(
                    qualifier,
                    "FSIM-SV-PARSE-254",
                    "compilation-unit 'virtual' must introduce a class");
                skip_to_semicolon();
            }
        } else if (match_keyword("class")) {
            compilation_unit_has_design_item_ = true;
            const auto declaration = previous();
            (void)require_standard(
                "a class declaration",
                StandardRevision::SystemVerilog2005,
                declaration,
                "FSIM-SV-PARSE-348");
            add_class_declaration(
                design.systemverilog_classes,
                parse_class(declaration, "$unit"),
                declaration);
        } else if (keyword("typedef") && keyword("class", 1)) {
            compilation_unit_has_design_item_ = true;
            const auto declaration = advance();
            add_class_declaration(
                design.systemverilog_classes,
                parse_class_forward_declaration(declaration, "$unit"),
                declaration);
        } else if (match_keyword("function")) {
            compilation_unit_has_design_item_ = true;
            const auto declaration = previous();
            if (compilation_unit_class_method_definition_start()) {
                design.systemverilog_class_method_definitions.push_back(
                    parse_class_out_of_block_method(
                        declaration, SystemVerilogClassMethodKind::Function));
            } else if (compilation_unit_dpi_export_definition_start(
                           design.systemverilog_dpi_declarations,
                           SystemVerilogDpiCallableKind::Function)) {
                auto function = parse_function(declaration);
                const bool duplicate = std::ranges::any_of(
                                           design.functions,
                                           [&](const FunctionDeclaration& existing) {
                                               return existing.name == function.name;
                                           })
                    || std::ranges::any_of(
                        design.tasks,
                        [&](const TaskDeclaration& existing) {
                            return existing.name == function.name;
                        });
                if (duplicate) {
                    error(
                        declaration,
                        "FSIM-SV-SEM-228",
                        "duplicate compilation-unit callable '"
                            + function.name + "'");
                } else {
                    design.functions.push_back(std::move(function));
                }
            } else {
                design.systemverilog_class_method_definitions.push_back(
                    parse_class_out_of_block_method(
                        declaration, SystemVerilogClassMethodKind::Function));
            }
        } else if (match_keyword("task")) {
            compilation_unit_has_design_item_ = true;
            const auto declaration = previous();
            if (compilation_unit_class_method_definition_start()) {
                design.systemverilog_class_method_definitions.push_back(
                    parse_class_out_of_block_method(
                        declaration, SystemVerilogClassMethodKind::Task));
            } else if (compilation_unit_dpi_export_definition_start(
                           design.systemverilog_dpi_declarations,
                           SystemVerilogDpiCallableKind::Task)) {
                auto task = parse_task(declaration);
                const bool duplicate = std::ranges::any_of(
                                           design.tasks,
                                           [&](const TaskDeclaration& existing) {
                                               return existing.name == task.name;
                                           })
                    || std::ranges::any_of(
                        design.functions,
                        [&](const FunctionDeclaration& existing) {
                            return existing.name == task.name;
                        });
                if (duplicate) {
                    error(
                        declaration,
                        "FSIM-SV-SEM-228",
                        "duplicate compilation-unit callable '" + task.name + "'");
                } else {
                    design.tasks.push_back(std::move(task));
                }
            } else {
                design.systemverilog_class_method_definitions.push_back(
                    parse_class_out_of_block_method(
                        declaration, SystemVerilogClassMethodKind::Task));
            }
        } else if (match_keyword("primitive")) {
            compilation_unit_has_design_item_ = true;
            auto declaration = parse_udp_declaration(previous());
            const bool duplicate = std::ranges::any_of(
                                       design.udp_declarations,
                                       [&](const VerilogUdpDeclaration& existing) {
                                           return existing.name == declaration.name;
                                       })
                || std::ranges::any_of(
                    design.units, [&](const DesignUnit& existing) {
                        return existing.name == declaration.name;
                    });
            if (duplicate) {
                error(
                    previous(),
                    "FSIM-SV-SEM-135",
                    "duplicate Verilog design declaration '"
                        + declaration.name + "'");
            } else {
                design.udp_declarations.push_back(std::move(declaration));
            }
        } else if (match_keyword("package")) {
            compilation_unit_has_design_item_ = true;
            (void)require_standard(
                "a package declaration",
                StandardRevision::SystemVerilog2005,
                previous(),
                "FSIM-SV-PARSE-348");
            auto package = parse_package(previous());
            auto& exports = package_constant_names_[package.name];
            for (const auto& parameter : package.parameters) {
                exports.insert(parameter.name);
            }
            design.units.push_back(std::move(package));
        } else if (dpi_declaration_start("import")) {
            compilation_unit_has_design_item_ = true;
            const auto declaration = advance();
            parse_dpi_declaration(
                design.systemverilog_dpi_declarations,
                declaration,
                SystemVerilogDpiDirection::Import,
                SystemVerilogDpiOwnerKind::CompilationUnit,
                "$unit");
        } else if (dpi_declaration_start("export")) {
            compilation_unit_has_design_item_ = true;
            const auto declaration = advance();
            parse_dpi_declaration(
                design.systemverilog_dpi_declarations,
                declaration,
                SystemVerilogDpiDirection::Export,
                SystemVerilogDpiOwnerKind::CompilationUnit,
                "$unit");
        } else if (match_keyword("import")) {
            compilation_unit_has_design_item_ = true;
            (void)require_standard(
                "a package import",
                StandardRevision::SystemVerilog2005,
                previous(),
                "FSIM-SV-PARSE-348");
            parse_import_clause(
                compilation_unit_imports_, previous());
        } else if (at(TokenKind::Backtick)) {
            parse_directive();
        } else if (at(TokenKind::Identifier)
            && structural_word_standard(current().text)) {
            const auto later_structure = advance();
            if (later_structure.text == "config"
                && standard_revision_
                    == StandardRevision::Verilog2001NoConfig
                && !compatibility_enabled("configuration")) {
                error(
                    later_structure,
                    "FSIM-SV-PARSE-348",
                    "configuration declarations are disabled by selected verilog-2001-noconfig; select verilog-2001 or verilog-2005");
            } else {
                (void)require_standard(
                    "structural form '" + later_structure.text + "'",
                    *structural_word_standard(later_structure.text),
                    later_structure,
                    "FSIM-SV-PARSE-348");
            }
            skip_to_semicolon();
        } else {
            const auto unexpected = advance();
            error(unexpected, "FSIM-SV-UNSUPPORTED-001",
                "unsupported compilation-unit item '" + unexpected.text + "'");
            skip_to_semicolon();
        }
    }
    if (!keyword_stack_.empty()) {
        error(
            current(),
            "FSIM-SV-PP-038",
            "unterminated `begin_keywords region");
        keyword_stack_.clear();
    }
    resolve_dpi_declarations(
        design.systemverilog_dpi_declarations,
        design.functions,
        design.tasks);
    normalize_udp_instances(design);
    return ParseResult { std::move(design), std::move(diagnostics_) };
}

[[nodiscard]] bool VerilogParser::keyword(
    const std::string_view text,
    const std::size_t lookahead,
    const bool case_insensitive) const  {
  if (!detail::ParserBase::keyword(
          text, lookahead, case_insensitive)) {
    return false;
  }
  return text.front() == '$' || keyword_reserved(keyword_set_, text);
}

[[nodiscard]] bool VerilogParser::any_keyword(
    const std::initializer_list<std::string_view> words,
    const bool case_insensitive) const  {
  return std::any_of(
      words.begin(), words.end(),
      [&](const std::string_view word) {
        return keyword(word, 0, case_insensitive);
      });
}

bool VerilogParser::match_keyword(
    const std::string_view text,
    const bool case_insensitive) {
  if (!keyword(text, 0, case_insensitive)) {
    return false;
  }
  advance();
  return true;
}

[[nodiscard]] bool VerilogParser::compatibility_enabled(
    const std::string_view name) const noexcept {
  return profile_has(compatibility_profile_, name);
}

bool VerilogParser::require_standard(
    const std::string_view feature,
    const StandardRevision required,
    const Token& token,
    const std::string_view diagnostic_code) {
  if (keyword_set_rank(keyword_set_for_standard_revision(standard_revision_))
      >= keyword_set_rank(keyword_set_for_standard_revision(required))) {
    return true;
  }
  error(
      token,
      std::string(diagnostic_code),
      std::string(feature) + " requires " + std::string(to_string(required))
          + "; selected " + std::string(to_string(standard_revision_))
          + ". Select that or a later standard revision, or rewrite the source form");
  return false;
}

Token VerilogParser::expect_keyword(
    const std::string_view word,
    const bool case_insensitive,
    std::string code) {
  if (keyword(word, 0, case_insensitive)) {
    return advance();
  }
  error(
      current(), std::move(code),
      "expected '" + std::string(word) + "'");
  return current();
}

SourceSpan VerilogParser::span_from(const Token& first, const Token& last) {
  return cover(first.span, last.span);
}

bool VerilogParser::verilog_attribute_instance_start() const
{
    return at(TokenKind::LeftParen) && at(TokenKind::Star, 1);
}

void VerilogParser::parse_verilog_attribute_instances()
{
    pending_systemverilog_fsm_pragmas_.clear();
    pending_systemverilog_fsm_pragma_target_position_.reset();
    const auto fsm_kind = [](const std::string_view name)
        -> std::optional<SystemVerilogFsmPragmaKind> {
        if (name == "fsm_current_state") {
            return SystemVerilogFsmPragmaKind::CurrentState;
        }
        if (name == "fsm_next_state") {
            return SystemVerilogFsmPragmaKind::NextState;
        }
        if (name == "fsm_legal_states") {
            return SystemVerilogFsmPragmaKind::LegalStates;
        }
        return std::nullopt;
    };
    while (verilog_attribute_instance_start()) {
        const auto start = advance();
        (void)advance();
        SystemVerilogFsmPragma fsm_pragma;
        bool require_specification = true;
        while (!at_end()
            && !(at(TokenKind::Star) && at(TokenKind::RightParen, 1))) {
            if (!at(TokenKind::Identifier)
                || keyword_reserved(keyword_set_, current().text)) {
                error(
                    current(),
                    "FSIM-SV-PARSE-336",
                    "an attribute specification requires an attribute name");
                while (!at_end() && !at(TokenKind::Comma)
                    && !(at(TokenKind::Star)
                        && at(TokenKind::RightParen, 1))) {
                    (void)advance();
                }
            } else {
                const auto name = advance();
                std::optional<Expression> value;
                if (match(TokenKind::Assign)) {
                    if (at(TokenKind::Comma)
                        || (at(TokenKind::Star)
                            && at(TokenKind::RightParen, 1))) {
                        error(
                            current(),
                            "FSIM-SV-PARSE-338",
                            "an attribute assignment requires a constant expression");
                    } else {
                        in_verilog_attribute_ = true;
                        value = parse_expression();
                        in_verilog_attribute_ = false;
                    }
                }
                if (language_ == Language::SystemVerilog2017) {
                    if (const auto kind = fsm_kind(name.text)) {
                        fsm_pragma.specifications.push_back(
                            SystemVerilogFsmPragmaSpecification {
                                *kind, std::move(value), name.span });
                    }
                }
            }
            require_specification = false;
            if (!match(TokenKind::Comma)) {
                break;
            }
            require_specification = true;
        }
        if (require_specification) {
            error(
                current(),
                "FSIM-SV-PARSE-336",
                "an attribute instance requires a specification after ','");
        }
        if (!match(TokenKind::Star)) {
            error(
                start,
                "FSIM-SV-PARSE-337",
                "an attribute instance is missing its closing '*)'");
            continue;
        }
        if (!match(TokenKind::RightParen)) {
            error(
                start,
                "FSIM-SV-PARSE-337",
                "an attribute instance is missing its closing '*)'");
        }
        if (!fsm_pragma.specifications.empty()) {
            fsm_pragma.span = span_from(start, previous());
            pending_systemverilog_fsm_pragmas_.push_back(
                std::move(fsm_pragma));
        }
    }
    if (!pending_systemverilog_fsm_pragmas_.empty()) {
        pending_systemverilog_fsm_pragma_target_position_ = position();
    }
}

std::string VerilogParser::string_literal_text(const Token& token) {
  if (token.text.size() >= 2 && token.text.front() == '"'
      && token.text.back() == '"') {
    return token.text.substr(1, token.text.size() - 2);
  }
  return token.text;
}

std::string VerilogParser::decoded_string_literal_text(const Token& token) {
  const auto spelling = string_literal_text(token);
  std::string result;
  result.reserve(spelling.size());
  for (std::size_t index = 0; index < spelling.size(); ++index) {
    const char current = spelling[index];
    if (current != '\\') {
      result.push_back(current);
      continue;
    }
    if (++index >= spelling.size()) {
      error(
          token,
          "FSIM-SV-SEM-040",
          "a Verilog string literal ends with an incomplete escape");
      break;
    }
    const char escaped = spelling[index];
    switch (escaped) {
    case 'n':
      result.push_back('\n');
      break;
    case 't':
      result.push_back('\t');
      break;
    case 'v':
        if (language_ == Language::SystemVerilog2017) {
            result.push_back('\v');
        } else {
            error(token, "FSIM-SV-SEM-040",
                "unsupported Verilog string escape '\\v'");
            result.push_back(escaped);
        }
        break;
    case 'f':
        if (language_ == Language::SystemVerilog2017) {
            result.push_back('\f');
        } else {
            error(token, "FSIM-SV-SEM-040",
                "unsupported Verilog string escape '\\f'");
            result.push_back(escaped);
        }
        break;
    case 'a':
        if (language_ == Language::SystemVerilog2017) {
            result.push_back('\a');
        } else {
            error(token, "FSIM-SV-SEM-040",
                "unsupported Verilog string escape '\\a'");
            result.push_back(escaped);
        }
        break;
    case 'x':
        if (language_ == Language::SystemVerilog2017) {
            const auto hexadecimal_value = [](const char character)
                -> std::optional<unsigned> {
                if (character >= '0' && character <= '9') {
                    return static_cast<unsigned>(character - '0');
                }
                if (character >= 'a' && character <= 'f') {
                    return 10U + static_cast<unsigned>(character - 'a');
                }
                if (character >= 'A' && character <= 'F') {
                    return 10U + static_cast<unsigned>(character - 'A');
                }
                return std::nullopt;
            };
            if (index + 2 >= spelling.size()) {
                error(token, "FSIM-SV-SEM-040",
                    "a SystemVerilog string hexadecimal escape requires two digits");
                break;
            }
            const auto high = hexadecimal_value(spelling[index + 1]);
            const auto low = hexadecimal_value(spelling[index + 2]);
            if (!high || !low) {
                error(token, "FSIM-SV-SEM-040",
                    "a SystemVerilog string hexadecimal escape requires two digits");
                break;
            }
            result.push_back(static_cast<char>((*high << 4U) | *low));
            index += 2;
            break;
        }
        error(token, "FSIM-SV-SEM-040",
            "unsupported Verilog string escape '\\x'");
        result.push_back(escaped);
        break;
    case '\\':
      result.push_back('\\');
      break;
    case '"':
      result.push_back('"');
      break;
    case '%':
    case '.':
      result.push_back(escaped);
      break;
    default:
      if (escaped >= '0' && escaped <= '7') {
        unsigned value = static_cast<unsigned>(escaped - '0');
        std::size_t digits = 1;
        while (digits < 3 && index + 1 < spelling.size()
               && spelling[index + 1] >= '0'
               && spelling[index + 1] <= '7') {
          value = value * 8U
              + static_cast<unsigned>(
                  spelling[++index] - '0');
          ++digits;
        }
        if (value > 255U) {
          error(
              token,
              "FSIM-SV-SEM-040",
              "a Verilog string octal escape exceeds one byte");
        } else {
          result.push_back(static_cast<char>(value));
        }
      } else {
        error(
            token,
            "FSIM-SV-SEM-040",
            "unsupported Verilog string escape '\\"
                + std::string(1, escaped) + "'");
        result.push_back(escaped);
      }
      break;
    }
  }
  return result;
}

Token VerilogParser::expect_identifier(std::string_view description) {
  if (at(TokenKind::Identifier)
      && !keyword_reserved(keyword_set_, current().text)) {
    return advance();
  }
  error(
      current(),
      "FSIM-SV-PARSE-001",
      "expected " + std::string(description)
          + ", found reserved keyword or non-identifier '"
          + current().text + "'");
  return current();
}

[[nodiscard]] bool VerilogParser::on_directive_line(const Token& tick) const  {
  return !at_end()
      && current().span.source_name == tick.span.source_name
      && current().span.begin.line == tick.span.begin.line;
}

void VerilogParser::reject_directive_arguments(
  const Token& tick,
  const Token& directive) {
  if (on_directive_line(tick)) {
    error(
        current(),
        "FSIM-SV-PP-032",
        "`" + directive.text + " does not accept arguments");
  }
}

void VerilogParser::reset_compiler_directives() {
  current_time_unit_magnitude_ = 1;
  current_time_unit_.clear();
  current_time_precision_.clear();
  current_default_nettype_ = "wire";
  current_cell_define_ = false;
  current_unconnected_drive_ = VerilogUnconnectedDrive::None;
}

void VerilogParser::parse_directive() {
  const auto tick = advance();
  const auto directive = at(TokenKind::Identifier) ? advance() : current();
  if (directive.text == "timescale") {
    parse_timescale(directive);
  } else if (directive.text == "default_nettype") {
    parse_default_nettype(tick, directive);
  } else if (directive.text == "resetall") {
    reset_compiler_directives();
    reject_directive_arguments(tick, directive);
  } else if (directive.text == "celldefine") {
    if (current_cell_define_) {
      error(
          directive,
          "FSIM-SV-PP-047",
          "nested or duplicate `celldefine is not legal");
    }
    current_cell_define_ = true;
    reject_directive_arguments(tick, directive);
  } else if (directive.text == "endcelldefine") {
    if (!current_cell_define_) {
      error(
          directive,
          "FSIM-SV-PP-033",
          "`endcelldefine without an active `celldefine");
    }
    current_cell_define_ = false;
    reject_directive_arguments(tick, directive);
  } else if (directive.text == "unconnected_drive") {
    parse_unconnected_drive(tick, directive);
  } else if (directive.text == "nounconnected_drive") {
    if (current_unconnected_drive_ == VerilogUnconnectedDrive::None) {
      error(
          directive,
          "FSIM-SV-PP-048",
          "`nounconnected_drive requires an active "
          "`unconnected_drive state");
    }
    current_unconnected_drive_ = VerilogUnconnectedDrive::None;
    reject_directive_arguments(tick, directive);
  } else if (directive.text == "begin_keywords") {
    parse_begin_keywords(tick, directive);
  } else if (directive.text == "end_keywords") {
    if (keyword_stack_.empty()) {
      error(
          directive,
          "FSIM-SV-PP-037",
          "`end_keywords without a matching `begin_keywords");
    } else {
      keyword_set_ = keyword_stack_.back();
      keyword_stack_.pop_back();
    }
    reject_directive_arguments(tick, directive);
  } else {
    error(directive, "FSIM-SV-UNSUPPORTED-002",
          "preprocessor directive `" + directive.text +
              " reached the parser without preprocessing");
  }
  const auto source_name = tick.span.source_name;
  const auto line = tick.span.begin.line;
  while (!at_end()
         && current().span.source_name == source_name
         && current().span.begin.line == line) {
    advance();
  }
}

void VerilogParser::parse_default_nettype(
  const Token& tick,
  const Token& directive) {
  if (!on_directive_line(tick)
      || current().kind != TokenKind::Identifier) {
    error(
        directive,
        "FSIM-SV-PP-034",
        "`default_nettype requires a standard net type or none");
    return;
  }
  const auto net_type = advance();
  constexpr std::string_view legal[] = {
      "wire", "tri", "tri0", "tri1", "wand", "triand",
      "wor", "trior", "trireg", "uwire", "none"};
  if (std::find(
          std::begin(legal), std::end(legal), net_type.text)
      == std::end(legal)) {
    error(
        net_type,
        "FSIM-SV-PP-034",
        "invalid `default_nettype value '" + net_type.text + "'");
  } else {
    current_default_nettype_ = net_type.text;
  }
  if (on_directive_line(tick)) {
    error(
        current(),
        "FSIM-SV-PP-034",
        "unexpected tokens after `default_nettype value");
  }
}

void VerilogParser::parse_unconnected_drive(
  const Token& tick,
  const Token& directive) {
  if (!on_directive_line(tick)
      || current().kind != TokenKind::Identifier) {
    error(
        directive,
        "FSIM-SV-PP-035",
        "`unconnected_drive requires pull0 or pull1");
    return;
  }
  const auto pull = advance();
  if (pull.text == "pull0") {
    current_unconnected_drive_ = VerilogUnconnectedDrive::Pull0;
  } else if (pull.text == "pull1") {
    current_unconnected_drive_ = VerilogUnconnectedDrive::Pull1;
  } else {
    error(
        pull,
        "FSIM-SV-PP-035",
        "`unconnected_drive requires pull0 or pull1");
  }
  if (on_directive_line(tick)) {
    error(
        current(),
        "FSIM-SV-PP-035",
        "unexpected tokens after `unconnected_drive value");
  }
}

void VerilogParser::parse_begin_keywords(
  const Token& tick,
  const Token& directive) {
  if (!on_directive_line(tick)
      || current().kind != TokenKind::StringLiteral) {
    error(
        directive,
        "FSIM-SV-PP-036",
        "`begin_keywords requires a quoted IEEE language version");
    return;
  }
  const auto version = advance();
  const auto spelling = string_literal_text(version);
  const auto parsed = parse_keyword_set(spelling);
  if (!parsed) {
    error(
        version,
        "FSIM-SV-PP-036",
        "unsupported `begin_keywords version '" + spelling + "'");
  } else if (keyword_set_rank(*parsed)
             > keyword_set_rank(
                 keyword_set_for_standard_revision(standard_revision_))) {
    error(
        version,
        "FSIM-SV-PP-052",
        "`begin_keywords version '" + spelling
            + "' is later than selected "
            + std::string(to_string(standard_revision_))
            + "; select that or a later standard revision");
  } else {
    keyword_stack_.push_back(keyword_set_);
    keyword_set_ = *parsed;
  }
  if (on_directive_line(tick)) {
    error(
        current(),
        "FSIM-SV-PP-036",
        "unexpected tokens after `begin_keywords version");
  }
}

} // namespace fsim::frontend
