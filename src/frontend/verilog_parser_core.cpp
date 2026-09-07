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
            (void)require_standard(
                "a program declaration",
                StandardRevision::SystemVerilog2005,
                previous(),
                "FSIM-SV-PARSE-348");
            design.units.push_back(parse_module(
                previous(), UnitKind::SystemVerilogProgram));
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

std::optional<std::uint64_t> VerilogParser::time_unit_femtoseconds(
  const std::string_view unit) {
  if (unit == "fs") {
    return 1;
  }
  if (unit == "ps") {
    return 1'000;
  }
  if (unit == "ns") {
    return 1'000'000;
  }
  if (unit == "us") {
    return 1'000'000'000;
  }
  if (unit == "ms") {
    return 1'000'000'000'000;
  }
  if (unit == "s") {
    return 1'000'000'000'000'000;
  }
  return std::nullopt;
}

[[nodiscard]] bool VerilogParser::time_declaration_start() const  {
  return at(TokenKind::Identifier)
      && (current().text == "timeunit"
          || current().text == "timeprecision")
      && (language_ == Language::SystemVerilog2017
          || at(TokenKind::Number, 1));
}

VerilogParser::DeclaredTime VerilogParser::parse_declared_time_value(
  const std::string_view description) {
  const auto magnitude = expect(
      TokenKind::Number,
      std::string{description} + " magnitude",
      "FSIM-SV-PARSE-131");
  const auto unit = expect(
      TokenKind::Identifier,
      std::string{description} + " unit",
      "FSIM-SV-PARSE-132");
  DeclaredTime result;
  result.span = cover(magnitude.span, unit.span);
  const auto parsed_magnitude = decimal_u64(magnitude.text);
  const auto factor = time_unit_femtoseconds(unit.text);
  if (!parsed_magnitude
      || (*parsed_magnitude != 1 && *parsed_magnitude != 10
          && *parsed_magnitude != 100)
      || !factor) {
    error(
        magnitude,
        "FSIM-SV-SEM-045",
        std::string{description}
            + " must use magnitude 1, 10, or 100 and unit "
              "fs, ps, ns, us, ms, or s");
    return result;
  }
  result.magnitude = *parsed_magnitude;
  result.unit = unit.text;
  result.valid = true;
  return result;
}

void VerilogParser::validate_effective_time_declaration(
  const Token& declaration,
  const std::uint64_t unit_magnitude,
  const std::string_view unit,
  const std::string_view precision) {
  if (unit.empty() || precision.empty()) {
    return;
  }
  const auto unit_factor = time_unit_femtoseconds(unit);
  const auto parsed_precision =
      parse_declared_time_spelling(precision);
  if (!unit_factor || !parsed_precision) {
    return;
  }
  if (unit_magnitude
          > std::numeric_limits<std::uint64_t>::max() / *unit_factor) {
    error(
        declaration,
        "FSIM-SV-SEM-048",
        "effective SystemVerilog time unit overflows");
    return;
  }
  const auto unit_fs = unit_magnitude * *unit_factor;
  if (parsed_precision->first
          > std::numeric_limits<std::uint64_t>::max()
              / parsed_precision->second
      || parsed_precision->first * parsed_precision->second > unit_fs) {
    error(
        declaration,
        "FSIM-SV-SEM-048",
        "SystemVerilog timeprecision cannot be coarser than timeunit");
  }
}

std::optional<std::pair<std::uint64_t, std::uint64_t>>
VerilogParser::parse_declared_time_spelling(const std::string_view spelling) {
  const auto split = std::find_if(
      spelling.begin(),
      spelling.end(),
      [](const char character) {
        return character < '0' || character > '9';
      });
  if (split == spelling.begin()) {
    return std::nullopt;
  }
  const auto magnitude = decimal_u64(
      spelling.substr(
          0,
          static_cast<std::size_t>(
              std::distance(spelling.begin(), split))));
  const auto factor = time_unit_femtoseconds(
      spelling.substr(
          static_cast<std::size_t>(
              std::distance(spelling.begin(), split))));
  if (!magnitude || !factor) {
    return std::nullopt;
  }
  return std::pair{*magnitude, *factor};
}

void VerilogParser::update_unit_time(DesignUnit& unit) {
  if (module_time_unit_.empty()) {
    unit.time_unit.clear();
  } else {
    unit.time_unit =
        std::to_string(module_time_unit_magnitude_)
        + module_time_unit_;
  }
  unit.time_precision = module_time_precision_;
}

void VerilogParser::parse_time_declaration(
  DesignUnit* unit,
  const Token& declaration) {
  const bool declares_unit = declaration.text == "timeunit";
  if (language_ != Language::SystemVerilog2017) {
    error(
        declaration,
        "FSIM-SV-SEM-044",
        "timeunit and timeprecision declarations require "
        "SystemVerilog-2017");
  }
  auto primary = parse_declared_time_value(declaration.text);
  std::optional<DeclaredTime> combined_precision;
  if (declares_unit && match(TokenKind::Slash)) {
    combined_precision =
        parse_declared_time_value("timeunit precision");
  }
  expect(
      TokenKind::Semicolon,
      "';' after time declaration",
      "FSIM-SV-PARSE-133");

  if (unit == nullptr && compilation_unit_has_design_item_) {
    error(
        declaration,
        "FSIM-SV-SEM-047",
        "compilation-unit time declarations must precede design items");
  }
  if (unit != nullptr && module_has_non_time_item_) {
    error(
        declaration,
        "FSIM-SV-SEM-047",
        "module/package time declarations must precede other items");
  }

  bool& unit_declared =
      unit == nullptr
          ? compilation_time_unit_declared_
          : module_time_unit_declared_;
  bool& precision_declared =
      unit == nullptr
          ? compilation_time_precision_declared_
          : module_time_precision_declared_;
  if (declares_unit) {
    if (unit_declared) {
      error(
          declaration,
          "FSIM-SV-SEM-046",
          "duplicate timeunit declaration in the same scope");
    }
    unit_declared = true;
  } else {
    if (precision_declared) {
      error(
          declaration,
          "FSIM-SV-SEM-046",
          "duplicate timeprecision declaration in the same scope");
    }
    precision_declared = true;
  }
  if (combined_precision) {
    if (precision_declared) {
      error(
          declaration,
          "FSIM-SV-SEM-046",
          "duplicate timeprecision declaration in the same scope");
    }
    precision_declared = true;
  }

  if (primary.valid) {
    if (unit == nullptr) {
      if (declares_unit) {
        compilation_time_unit_magnitude_ = primary.magnitude;
        compilation_time_unit_ = primary.unit;
      } else {
        compilation_time_precision_ = primary.spelling();
      }
    } else if (declares_unit) {
      module_time_unit_magnitude_ = primary.magnitude;
      module_time_unit_ = primary.unit;
    } else {
      module_time_precision_ = primary.spelling();
    }
  }
  if (combined_precision && combined_precision->valid) {
    if (unit == nullptr) {
      compilation_time_precision_ =
          combined_precision->spelling();
    } else {
      module_time_precision_ =
          combined_precision->spelling();
    }
  }

  const auto effective_unit_magnitude =
      unit == nullptr
          ? (compilation_time_unit_.empty()
                 ? current_time_unit_magnitude_
                 : compilation_time_unit_magnitude_)
          : module_time_unit_magnitude_;
  const auto& effective_unit =
      unit == nullptr
          ? (compilation_time_unit_.empty()
                 ? current_time_unit_
                 : compilation_time_unit_)
          : module_time_unit_;
  const auto& effective_precision =
      unit == nullptr
          ? (compilation_time_precision_.empty()
                 ? current_time_precision_
                 : compilation_time_precision_)
          : module_time_precision_;
  validate_effective_time_declaration(
      declaration,
      effective_unit_magnitude,
      effective_unit,
      effective_precision);
  if (unit != nullptr) {
    update_unit_time(*unit);
  }
}

void VerilogParser::parse_timescale(const Token& directive) {
  const auto unit_magnitude_token = expect(
      TokenKind::Number,
      "time-unit magnitude after `timescale",
      "FSIM-SV-PARSE-039");
  const auto unit_token = expect(
      TokenKind::Identifier,
      "time-unit name after `timescale magnitude",
      "FSIM-SV-PARSE-040");
  expect(
      TokenKind::Slash,
      "'/' between `timescale unit and precision",
      "FSIM-SV-PARSE-041");
  const auto precision_magnitude_token = expect(
      TokenKind::Number,
      "time-precision magnitude after '/'",
      "FSIM-SV-PARSE-042");
  const auto precision_token = expect(
      TokenKind::Identifier,
      "time-precision unit",
      "FSIM-SV-PARSE-043");

  const auto unit_magnitude =
      decimal_u64(unit_magnitude_token.text);
  const auto precision_magnitude =
      decimal_u64(precision_magnitude_token.text);
  const auto unit_factor = time_unit_femtoseconds(unit_token.text);
  const auto precision_factor =
      time_unit_femtoseconds(precision_token.text);
  const auto legal_magnitude = [](const std::uint64_t value) {
    return value == 1 || value == 10 || value == 100;
  };
  if (!unit_magnitude || !precision_magnitude
      || !legal_magnitude(*unit_magnitude)
      || !legal_magnitude(*precision_magnitude)
      || !unit_factor || !precision_factor) {
    error(
        directive,
        "FSIM-SV-SEM-007",
        "`timescale magnitudes must be 1, 10, or 100 and units must "
        "be fs, ps, ns, us, ms, or s");
    return;
  }
  if (*unit_magnitude
          > std::numeric_limits<std::uint64_t>::max() / *unit_factor
      || *precision_magnitude
          > std::numeric_limits<std::uint64_t>::max()
              / *precision_factor) {
    error(
        directive,
        "FSIM-SV-SEM-008",
        "`timescale value exceeds fsim's 64-bit time range");
    return;
  }
  const auto unit_fs = *unit_magnitude * *unit_factor;
  const auto precision_fs = *precision_magnitude * *precision_factor;
  if (precision_fs > unit_fs) {
    error(
        directive,
        "FSIM-SV-SEM-009",
        "`timescale precision cannot be coarser than its time unit");
    return;
  }
  current_time_unit_magnitude_ = *unit_magnitude;
  current_time_unit_ = unit_token.text;
  current_time_precision_ =
      std::to_string(*precision_magnitude) + precision_token.text;
}

void VerilogParser::note_implicit_net_reference(const Token& name) {
  for (const auto& import_item : active_package_imports_) {
    if ((!import_item.name.empty()
         && import_item.name == name.text)
        || (import_item.name.empty()
            && package_constant_names_[
                   import_item.package].contains(name.text))) {
      return;
    }
  }
  if (!current_procedural_names_.contains(name.text)
      && !current_generate_names_.contains(name.text)
      && !current_loop_names_.contains(name.text)) {
    implicit_net_references_.push_back(
        {name.text, current_default_nettype_, name.span,
         name.expansion_stack});
  }
}

void VerilogParser::resolve_implicit_nets(DesignUnit& unit) {
  std::unordered_set<std::string> known;
  known.insert(
      declared_genvars_.begin(), declared_genvars_.end());
  for (const auto& parameter : unit.parameters) {
    known.insert(parameter.name);
  }
  for (const auto& port : unit.ports) {
    known.insert(port.name);
  }
  for (const auto& signal : unit.signals) {
    known.insert(signal.name);
  }
  for (const auto& variable : unit.variables) {
    known.insert(variable.name);
  }
  for (const auto& instance : unit.instances) {
    known.insert(instance.name);
  }
  for (const auto& block : unit.systemverilog_clocking_blocks) {
    known.insert(block.name);
  }
  std::unordered_set<std::string> rejected;
  for (const auto& reference : implicit_net_references_) {
    const auto member_separator = reference.name.find('.');
    if (member_separator != std::string::npos
        && known.contains(reference.name.substr(0, member_separator))) {
      continue;
    }
    if (known.contains(reference.name)
        || rejected.contains(reference.name)) {
      continue;
    }
    if (container_iterator_names_.contains(reference.name)) {
      diagnostics_.push_back({
          DiagnosticSeverity::Error,
          "FSIM-SV-SEM-090",
          "container iterator '" + reference.name
              + "' is visible only inside its with-clause expression",
          reference.span,
          reference.expansion_stack});
      rejected.insert(reference.name);
      continue;
    }
    if (reference.net_type == "none") {
      diagnostics_.push_back({
          DiagnosticSeverity::Error,
          "FSIM-SV-SEM-015",
          "implicit net '" + reference.name
              + "' is forbidden by `default_nettype none",
          reference.span,
          reference.expansion_stack});
      rejected.insert(reference.name);
      continue;
    }
    Type type{
        ValueDomain::Logic4,
        reference.net_type,
        std::nullopt,
        false};
    unit.signals.push_back({
        reference.name,
        std::move(type),
        PortDirection::Unknown,
        false,
        reference.span});
    known.insert(reference.name);
  }
}

DesignUnit VerilogParser::parse_module(
    const Token& start,
    const UnitKind kind,
    const bool extern_declaration)
{
    non_ansi_ports_.clear();
    body_port_declarations_.clear();
    port_type_refinements_.clear();
    implicit_net_references_.clear();
    container_iterator_names_.clear();
    current_procedural_names_.clear();
    current_procedural_types_.clear();
    current_generate_names_.clear();
    current_loop_names_.clear();
    declared_genvars_.clear();
    external_genvar_uses_.clear();
    next_implicit_generate_scope_ = 1;
    module_time_unit_magnitude_ = compilation_time_unit_.empty()
        ? current_time_unit_magnitude_
        : compilation_time_unit_magnitude_;
    module_time_unit_ = compilation_time_unit_.empty()
        ? current_time_unit_
        : compilation_time_unit_;
    module_time_precision_ = compilation_time_precision_.empty()
        ? current_time_precision_
        : compilation_time_precision_;
    module_time_unit_declared_ = false;
    module_time_precision_declared_ = false;
    module_has_non_time_item_ = false;
    DesignUnit unit;
    if (pending_systemverilog_fsm_pragma_target_position_
            && *pending_systemverilog_fsm_pragma_target_position_ + 1U
                == position()) {
        unit.systemverilog_fsm_pragmas
            = std::move(pending_systemverilog_fsm_pragmas_);
    }
    pending_systemverilog_fsm_pragmas_.clear();
    pending_systemverilog_fsm_pragma_target_position_.reset();
    unit.kind = kind;
    const bool interface_unit = kind == UnitKind::SystemVerilogInterface;
    const bool program_unit = kind == UnitKind::SystemVerilogProgram;
    const auto unit_kind = interface_unit
        ? std::string_view { "interface" }
        : program_unit ? std::string_view { "program" }
                       : std::string_view { "module" };
    unit.language = language_;
    unit.standard_revision = standard_revision_;
    unit.verilog_compatibility_profile = compatibility_profile_;
    unit.systemverilog_imports = compilation_unit_imports_;
    active_package_imports_ = unit.systemverilog_imports;
    unit.default_nettype = current_default_nettype_;
    unit.is_cell = current_cell_define_;
    unit.systemverilog_extern = extern_declaration;
    update_unit_time(unit);
    if (keyword("automatic") || keyword("static")
        || (at(TokenKind::Identifier)
            && (current().text == "automatic"
                || current().text == "static"))) {
        const auto lifetime = advance();
        (void)require_standard(
            std::string(unit_kind) + " lifetime '" + lifetime.text + "'",
            StandardRevision::SystemVerilog2005,
            lifetime);
    }
    const auto name = expect_identifier(
        std::string { unit_kind } + " name");
    unit.name = name.text;

    if (match(TokenKind::Hash)) {
        const auto hash = previous();
        (void)require_standard(
            "a module parameter port list",
            StandardRevision::Verilog2001,
            hash);
        parse_parameter_port_list(unit, hash);
    }

    if (match(TokenKind::LeftParen)) {
        parse_module_ports(unit);
        expect(TokenKind::RightParen, "')' after module ports",
            "FSIM-SV-PARSE-002");
    }
    expect(TokenKind::Semicolon,
        "';' after " + std::string { unit_kind } + " header",
        "FSIM-SV-PARSE-003");

    if (extern_declaration) {
        unit.span = span_from(start, previous());
        return unit;
    }

    const auto terminator = interface_unit
        ? std::string_view { "endinterface" }
        : program_unit ? std::string_view { "endprogram" }
                       : std::string_view { "endmodule" };
    while (!at_end() && !keyword(terminator)) {
        const auto before = position();
        if (verilog_attribute_instance_start()) {
            parse_verilog_attribute_instances();
        } else if (time_declaration_start()) {
            const auto declaration = advance();
            parse_time_declaration(&unit, declaration);
        } else if (match_keyword("parameter")) {
            module_has_non_time_item_ = true;
            parse_parameter_group(unit, false, false, previous());
        } else if (match_keyword("localparam")) {
            module_has_non_time_item_ = true;
            parse_parameter_group(unit, true, false, previous());
        } else if (match_keyword("defparam")) {
            module_has_non_time_item_ = true;
            parse_defparam_declaration(
                unit.verilog_defparams, previous());
        } else if (match_keyword("bind")) {
            module_has_non_time_item_ = true;
            (void)require_standard(
                "a bind directive", StandardRevision::SystemVerilog2005,
                previous(), "FSIM-SV-PARSE-348");
            unit.systemverilog_binds.push_back(
                parse_systemverilog_bind(previous()));
        } else if (dpi_declaration_start("import")) {
            module_has_non_time_item_ = true;
            const auto declaration = advance();
            parse_dpi_declaration(
                unit.systemverilog_dpi_declarations,
                declaration,
                SystemVerilogDpiDirection::Import,
                SystemVerilogDpiOwnerKind::DesignUnit,
                unit.name);
        } else if (dpi_declaration_start("export")) {
            module_has_non_time_item_ = true;
            const auto declaration = advance();
            parse_dpi_declaration(
                unit.systemverilog_dpi_declarations,
                declaration,
                SystemVerilogDpiDirection::Export,
                SystemVerilogDpiOwnerKind::DesignUnit,
                unit.name);
        } else if (match_keyword("import")) {
            module_has_non_time_item_ = true;
            (void)require_standard(
                "a package import", StandardRevision::SystemVerilog2005,
                previous(), "FSIM-SV-PARSE-348");
            parse_import_clause(
                unit.systemverilog_imports, previous());
            active_package_imports_ = unit.systemverilog_imports;
        } else if (match_keyword("nettype")) {
            module_has_non_time_item_ = true;
            parse_nettype(unit, previous());
        } else if (match_keyword("alias")) {
            module_has_non_time_item_ = true;
            (void)require_standard(
                "an alias statement", StandardRevision::SystemVerilog2005,
                previous(), "FSIM-SV-PARSE-348");
            parse_alias_statement(unit, previous());
        } else if (match_keyword("let")) {
            module_has_non_time_item_ = true;
            (void)require_standard(
                "a let declaration", StandardRevision::SystemVerilog2009,
                previous(), "FSIM-SV-PARSE-348");
            parse_let_declaration(unit, previous());
        } else if (match_keyword("typedef")) {
            module_has_non_time_item_ = true;
            const auto declaration = previous();
            (void)require_standard(
                "a typedef declaration", StandardRevision::SystemVerilog2005,
                declaration, "FSIM-SV-PARSE-348");
            if (keyword("class")) {
                add_class_declaration(
                    unit.systemverilog_classes,
                    parse_class_forward_declaration(declaration, unit.name),
                    declaration);
            } else {
                parse_typedef(unit, declaration);
            }
        } else if (match_keyword("virtual")) {
            module_has_non_time_item_ = true;
            const auto qualifier = previous();
            if (match_keyword("class")) {
                add_class_declaration(
                    unit.systemverilog_classes,
                    parse_class(previous(), unit.name, true),
                    qualifier);
            } else {
                parse_virtual_interface_declaration(
                    unit, qualifier);
            }
        } else if (match_keyword("interface")) {
            module_has_non_time_item_ = true;
            const auto qualifier = previous();
            if (match_keyword("class")) {
                (void)require_standard(
                    "an interface class",
                    StandardRevision::SystemVerilog2012,
                    previous(),
                    "FSIM-SV-PARSE-348");
                add_class_declaration(
                    unit.systemverilog_classes,
                    parse_class(previous(), unit.name, false, true),
                    qualifier);
            } else {
                error(
                    qualifier,
                    "FSIM-SV-PARSE-255",
                    "a nested interface declaration must be an interface class");
                skip_to_semicolon();
            }
        } else if (match_keyword("class")) {
            module_has_non_time_item_ = true;
            const auto declaration = previous();
            (void)require_standard(
                "a class declaration", StandardRevision::SystemVerilog2005,
                declaration, "FSIM-SV-PARSE-348");
            add_class_declaration(
                unit.systemverilog_classes,
                parse_class(declaration, unit.name),
                declaration);
        } else if (match_keyword("modport")) {
            module_has_non_time_item_ = true;
            (void)require_standard(
                "a modport declaration", StandardRevision::SystemVerilog2005,
                previous(), "FSIM-SV-PARSE-348");
            if (!interface_unit) {
                error(
                    previous(),
                    "FSIM-SV-SEM-115",
                    "a modport declaration is only valid inside an interface");
                skip_to_semicolon();
            } else {
                parse_modport(unit, previous());
            }
        } else if (match_keyword("clocking")) {
            module_has_non_time_item_ = true;
            (void)require_standard(
                "a clocking block", StandardRevision::SystemVerilog2005,
                previous(), "FSIM-SV-PARSE-348");
            parse_clocking_block(unit, previous());
        } else if (match_keyword("default")) {
            module_has_non_time_item_ = true;
            const auto default_start = previous();
            if (!match_keyword("clocking")) {
                error(
                    default_start,
                    "FSIM-SV-SEM-187",
                    "a design-unit default declaration must select a clocking block");
                skip_to_semicolon();
            } else {
                parse_default_clocking(unit, default_start);
            }
        } else if (
            at(TokenKind::Identifier)
            && current().text == "covergroup") {
            module_has_non_time_item_ = true;
            const auto declaration_start = advance();
            (void)require_standard(
                "a covergroup declaration",
                StandardRevision::SystemVerilog2005,
                declaration_start, "FSIM-SV-PARSE-348");
            add_covergroup_declaration(
                unit.systemverilog_covergroups,
                parse_covergroup_declaration(
                    declaration_start,
                    SystemVerilogCovergroupOwnerKind::DesignUnit),
                declaration_start);
        } else if (
            at(TokenKind::Identifier)
            && contains_word(
                { "sequence", "property", "checker" },
                current().text)) {
            module_has_non_time_item_ = true;
            const auto declaration_start = advance();
            (void)require_standard(
                "an assertion declaration '" + declaration_start.text + "'",
                declaration_start.text == "checker"
                    ? StandardRevision::SystemVerilog2009
                    : StandardRevision::SystemVerilog2005,
                declaration_start,
                "FSIM-SV-PARSE-348");
            const auto assertion_kind = declaration_start.text == "sequence"
                ? SystemVerilogAssertionDeclarationKind::Sequence
                : declaration_start.text == "property"
                ? SystemVerilogAssertionDeclarationKind::Property
                : SystemVerilogAssertionDeclarationKind::Checker;
            auto declaration = parse_assertion_declaration(declaration_start, assertion_kind);
            const auto duplicate = std::ranges::any_of(
                unit.systemverilog_assertion_declarations,
                [&](const SystemVerilogAssertionDeclaration& existing) {
                    return !declaration.name.empty()
                        && existing.name == declaration.name;
                });
            if (duplicate) {
                error(
                    declaration_start,
                    "FSIM-SV-SEM-192",
                    "duplicate assertion declaration '" + declaration.name + "'");
            } else {
                unit.systemverilog_assertion_declarations.push_back(
                    std::move(declaration));
            }
        } else if (
            (at(TokenKind::Identifier)
                && contains_word(
                    { "assert", "assume", "cover", "restrict" },
                    current().text))
            || (at(TokenKind::Identifier)
                && at(TokenKind::Colon, 1)
                && at(TokenKind::Identifier, 2)
                && contains_word(
                    { "assert", "assume", "cover", "restrict" },
                    current(2).text))) {
            module_has_non_time_item_ = true;
            std::optional<Token> label;
            if (at(TokenKind::Colon, 1)) {
                label = advance();
                advance();
            }
            const auto directive = advance();
            (void)require_standard(
                "a concurrent assertion",
                StandardRevision::SystemVerilog2005,
                directive,
                "FSIM-SV-PARSE-348");
            const auto assertion_kind = directive.text == "assert"
                ? SystemVerilogConcurrentAssertionKind::Assert
                : directive.text == "assume"
                ? SystemVerilogConcurrentAssertionKind::Assume
                : directive.text == "cover"
                ? SystemVerilogConcurrentAssertionKind::Cover
                : SystemVerilogConcurrentAssertionKind::Restrict;
            unit.systemverilog_concurrent_assertions.push_back(
                parse_concurrent_assertion(
                    directive, assertion_kind, std::move(label)));
        } else if (match_keyword("function")) {
            module_has_non_time_item_ = true;
            auto function = parse_function(previous());
            const bool duplicate = std::ranges::any_of(
                                       unit.functions,
                                       [&](const FunctionDeclaration& existing) {
                                           return existing.name == function.name;
                                       })
                || std::ranges::any_of(
                    unit.tasks,
                    [&](const TaskDeclaration& existing) {
                        return existing.name == function.name;
                    });
            if (duplicate) {
                error(
                    start,
                    "FSIM-SV-SEM-066",
                    "duplicate module function '" + function.name + "'");
            } else {
                unit.functions.push_back(std::move(function));
            }
        } else if (match_keyword("task")) {
            module_has_non_time_item_ = true;
            auto task = parse_task(previous());
            const bool duplicate = std::ranges::any_of(
                                       unit.tasks,
                                       [&](const TaskDeclaration& existing) {
                                           return existing.name == task.name;
                                       })
                || std::ranges::any_of(
                    unit.functions,
                    [&](const FunctionDeclaration& existing) {
                        return existing.name == task.name;
                    });
            if (duplicate) {
                error(
                    start,
                    "FSIM-SV-SEM-073",
                    "duplicate module task '" + task.name + "'");
            } else {
                unit.tasks.push_back(std::move(task));
            }
        } else if (match_keyword("genvar")) {
            module_has_non_time_item_ = true;
            parse_genvar_declaration(unit);
        } else if (match_keyword("event")) {
            module_has_non_time_item_ = true;
            parse_event_declaration(unit, previous());
        } else if (match_keyword("specify")) {
            module_has_non_time_item_ = true;
            if (interface_unit || program_unit) {
                error(
                    previous(),
                    "FSIM-SV-SEM-164",
                    "a specify block is legal only in a module");
            }
            auto block = parse_specify_block(previous());
            for (const auto& declaration : block.specparams) {
                const bool duplicate = std::ranges::any_of(
                    unit.verilog_specify_blocks,
                    [&](const VerilogSpecifyBlock& existing_block) {
                        return std::ranges::any_of(
                            existing_block.specparams,
                            [&](const VerilogSpecparamDeclaration& existing) {
                                return existing.name == declaration.name;
                            });
                    });
                if (duplicate) {
                    error(
                        Token {
                            TokenKind::Identifier,
                            declaration.name,
                            declaration.span,
                            { } },
                        "FSIM-SV-SEM-165",
                        "duplicate specparam declaration '"
                            + declaration.name + "'");
                }
            }
            unit.verilog_specify_blocks.push_back(std::move(block));
        } else if (
            keyword("final")
            || (language_ == Language::Verilog2005
                && at(TokenKind::Identifier)
                && current().text == "final")) {
            module_has_non_time_item_ = true;
            unit.processes.push_back(parse_final());
        } else if (
            at(TokenKind::Identifier)
            && structural_word_standard(current().text)
            && !keyword_reserved(keyword_set_, current().text)) {
            module_has_non_time_item_ = true;
            const auto later_structure = advance();
            (void)require_standard(
                "structural form '" + later_structure.text + "'",
                *structural_word_standard(later_structure.text),
                later_structure,
                "FSIM-SV-PARSE-348");
            skip_to_semicolon();
        } else if (
            at(TokenKind::Identifier)
            && declaration_word_standard(current().text)
            && !keyword_reserved(keyword_set_, current().text)) {
            module_has_non_time_item_ = true;
            const auto later_declaration = advance();
            const auto required =
                later_declaration.text == "automatic"
                    || later_declaration.text == "static"
                ? StandardRevision::SystemVerilog2005
                : *declaration_word_standard(later_declaration.text);
            (void)require_standard(
                "declaration form '" + later_declaration.text + "'",
                required,
                later_declaration);
            skip_to_semicolon();
        } else if (is_declaration_start() && !instance_start()) {
            module_has_non_time_item_ = true;
            parse_declaration(unit);
        } else if (is_gate_primitive()) {
            module_has_non_time_item_ = true;
            parse_gate_primitive(
                unit.concurrent_statements, unit.signals, unit.ports);
        } else if (any_keyword({ "cmos", "rcmos", "nmos", "pmos", "rnmos",
                       "rpmos", "tran", "rtran", "tranif0", "tranif1",
                       "rtranif0", "rtranif1", "pullup", "pulldown" })) {
            module_has_non_time_item_ = true;
            parse_switch_primitive(
                unit.concurrent_statements, unit.signals, unit.ports);
        } else if (match_keyword("assign")) {
            module_has_non_time_item_ = true;
            auto assignments = parse_continuous_assignments(previous());
            unit.concurrent_statements.insert(
                unit.concurrent_statements.end(),
                std::make_move_iterator(assignments.begin()),
                std::make_move_iterator(assignments.end()));
        } else if (
            keyword("always") || keyword("always_ff")
            || keyword("always_comb") || keyword("always_latch")
            || (language_ == Language::Verilog2005
                && at(TokenKind::Identifier)
                && (current().text == "always_ff"
                    || current().text == "always_comb"
                    || current().text == "always_latch"))) {
            module_has_non_time_item_ = true;
            unit.processes.push_back(parse_always());
        } else if (keyword("initial")) {
            module_has_non_time_item_ = true;
            unit.processes.push_back(parse_initial());
        } else if (match_keyword("generate")) {
            module_has_non_time_item_ = true;
            (void)require_standard(
                "a generate region", StandardRevision::Verilog2001,
                previous(), "FSIM-SV-PARSE-348");
            parse_generate_region(unit, previous());
        } else if (match_keyword("if")) {
            module_has_non_time_item_ = true;
            (void)require_standard(
                "a conditional generate", StandardRevision::Verilog2001,
                previous(), "FSIM-SV-PARSE-348");
            unit.generate_regions.push_back(
                parse_conditional_generate(previous()));
        } else if (match_keyword("for")) {
            module_has_non_time_item_ = true;
            (void)require_standard(
                "an iterative generate", StandardRevision::Verilog2001,
                previous(), "FSIM-SV-PARSE-348");
            unit.generate_regions.push_back(
                parse_iterative_generate(previous()));
        } else if (match_keyword("case")) {
            module_has_non_time_item_ = true;
            (void)require_standard(
                "a selection generate", StandardRevision::Verilog2001,
                previous(), "FSIM-SV-PARSE-348");
            unit.generate_regions.push_back(
                parse_selection_generate(previous()));
        } else if (instance_start()) {
            module_has_non_time_item_ = true;
            auto instances = parse_instances();
            unit.instances.insert(
                unit.instances.end(),
                std::make_move_iterator(instances.begin()),
                std::make_move_iterator(instances.end()));
        } else if (at(TokenKind::Backtick)) {
            parse_directive();
        } else {
            module_has_non_time_item_ = true;
            const auto unexpected = advance();
            error(
                unexpected,
                "FSIM-SV-UNSUPPORTED-004",
                "unsupported " + std::string { unit_kind }
                    + " item starting with '" + unexpected.text + "'");
            skip_to_semicolon();
        }
        if (position() == before) {
            advance();
        }
    }
    expect_keyword(terminator, false, "FSIM-SV-PARSE-004");
    if (match(TokenKind::Colon)) {
        const auto end_name = expect_identifier(
            std::string { unit_kind } + " name after "
            + std::string { terminator });
        if (end_name.text != unit.name) {
            error(
                end_name,
                "FSIM-SV-SEM-129",
                std::string { unit_kind }
                    + " end name does not match '" + unit.name + "'");
        }
    }
    for (const auto& use : external_genvar_uses_) {
        if (!declared_genvars_.contains(use.text)) {
            error(
                use,
                "FSIM-SV-PARSE-066",
                "generate loop variable '" + use.text
                    + "' is not declared by inline or module-scope genvar");
        }
    }
    resolve_implicit_nets(unit);
    resolve_assertion_references(unit);
    resolve_dpi_declarations(
        unit.systemverilog_dpi_declarations,
        unit.functions,
        unit.tasks);
    pending_systemverilog_fsm_pragmas_.clear();
    pending_systemverilog_fsm_pragma_target_position_.reset();
    unit.span = span_from(start, previous());
    return unit;
}

bool VerilogParser::instance_start() const {
  if (!at(TokenKind::Identifier)) {
    return false;
  }
  if (keyword_reserved(keyword_set_, current().text)) {
    return false;
  }
  if (at(TokenKind::LeftParen, 1) || at(TokenKind::Hash, 1)) {
    return true;
  }
  if (!at(TokenKind::Identifier, 1)) {
    return false;
  }
  if (at(TokenKind::LeftParen, 2)) {
    return true;
  }
  if (!at(TokenKind::LeftBracket, 2)) {
    return false;
  }
  std::size_t depth = 1;
  for (std::size_t lookahead = 3;
       !at(TokenKind::EndOfFile, lookahead); ++lookahead) {
    if (at(TokenKind::LeftBracket, lookahead)) {
      ++depth;
    } else if (at(TokenKind::RightBracket, lookahead)
               && --depth == 0) {
      return at(TokenKind::LeftParen, lookahead + 1);
    }
  }
  return false;
}

}  // namespace fsim::frontend
