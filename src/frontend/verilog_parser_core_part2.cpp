// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

namespace fsim::frontend {

void VerilogParser::parse_import_clause(
    std::vector<SystemVerilogImport>& imports,
    const Token& start) {
  for (;;) {
    const auto package = expect_identifier("package name in import");
    expect(
        TokenKind::Scope,
        "'::' after imported package name",
        "FSIM-SV-PARSE-078");
    std::string name;
    if (match(TokenKind::Star)) {
      name.clear();
    } else {
      name = expect_identifier("imported package item").text;
    }
    imports.push_back({
        package.text,
        std::move(name),
        cover(start.span, previous().span)});
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after package import",
      "FSIM-SV-PARSE-079");
}

void VerilogParser::parse_export_clause(
    std::vector<SystemVerilogExport>& exports,
    const Token& start) {
  for (;;) {
    std::string package;
    if (match(TokenKind::Star)) {
      package = "*";
    } else {
      package = expect_identifier("package name in export").text;
    }
    expect(
        TokenKind::Scope,
        "'::' after exported package name",
        "FSIM-SV-PARSE-215");
    std::string name;
    if (!match(TokenKind::Star)) {
      name = expect_identifier("exported package item").text;
    }
    exports.push_back({
        std::move(package),
        std::move(name),
        cover(start.span, previous().span)});
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after package export",
      "FSIM-SV-PARSE-216");
}

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

[[maybe_unused]] [[nodiscard]] KeywordSet compatibility_keyword_set(
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
    unit.systemverilog_assertion_declarations
        = compilation_unit_checkers_;
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
    if (language_ == Language::SystemVerilog2017) {
        unit.systemverilog_scheduling_declaration
            = SystemVerilogDesignSchedulingDeclaration {
                program_unit
                    ? SystemVerilogProcessRegion::Reactive
                    : SystemVerilogProcessRegion::Active,
                extern_declaration,
                start.span
            };
    }
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

    bool has_header_import = false;
    while (match_keyword("import")) {
        has_header_import = true;
        module_has_non_time_item_ = true;
        (void)require_standard(
            "a package import in a design-unit header",
            StandardRevision::SystemVerilog2005,
            previous(),
            "FSIM-SV-PARSE-348");
        parse_import_clause(
            unit.systemverilog_imports, previous());
        active_package_imports_ = unit.systemverilog_imports;
    }

    const bool has_parameter_port_list = match(TokenKind::Hash);
    if (has_parameter_port_list) {
        const auto hash = previous();
        (void)require_standard(
            "a module parameter port list",
            StandardRevision::Verilog2001,
            hash);
        parse_parameter_port_list(unit, hash);
    }
    const bool body_parameters_are_local = has_parameter_port_list
        && kind == UnitKind::VerilogModule
        && (language_ == Language::SystemVerilog2017
            || !unit.parameters.empty());

    const bool has_port_list = match(TokenKind::LeftParen);
    if (has_port_list) {
        parse_module_ports(unit);
        expect(TokenKind::RightParen, "')' after module ports",
            "FSIM-SV-PARSE-002");
    }
    if (has_header_import
        && !has_parameter_port_list
        && !has_port_list) {
        error(
            name,
            "FSIM-SV-PARSE-385",
            "a design-unit header package import must be followed by "
            "a parameter-port list or port list");
    }
    expect(TokenKind::Semicolon,
        "';' after " + std::string { unit_kind } + " header",
        "FSIM-SV-PARSE-003");

    if (extern_declaration) {
        unit.span = span_from(start, previous());
        return unit;
    }

    const auto previous_program_generate_context
        = program_generate_context_;
    program_generate_context_ = program_unit;

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
            parse_parameter_group(
                unit,
                body_parameters_are_local,
                false,
                previous());
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
        } else if (program_unit
            && (keyword("$fatal") || keyword("$error")
                || keyword("$warning") || keyword("$info"))) {
            module_has_non_time_item_ = true;
            (void)require_standard(
                "a program elaboration severity system task",
                StandardRevision::SystemVerilog2023,
                current(),
                "FSIM-SV-PARSE-381");
            if (auto statement = parse_statement()) {
                unit.concurrent_statements.push_back(
                    std::move(*statement));
            }
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
        } else if (
            instance_start()
            && std::ranges::any_of(
                unit.systemverilog_assertion_declarations,
                [&](const SystemVerilogAssertionDeclaration& declaration) {
                    return declaration.kind
                            == SystemVerilogAssertionDeclarationKind::Checker
                        && declaration.name == current().text;
                })) {
            module_has_non_time_item_ = true;
            auto instances = parse_checker_instances();
            for (auto& instance : instances) {
                const auto duplicate = std::ranges::any_of(
                                           unit.systemverilog_checker_instances,
                                           [&](const auto& existing) {
                                               return existing.name
                                                   == instance.name;
                                           })
                    || std::ranges::any_of(
                        unit.instances,
                        [&](const auto& existing) {
                            return existing.name == instance.name;
                        });
                if (duplicate) {
                    error(
                        Token {
                            TokenKind::Identifier,
                            instance.name,
                            instance.span,
                            { } },
                        "FSIM-SV-SEM-256",
                        "duplicate checker instance '" + instance.name + "'");
                } else {
                    unit.systemverilog_checker_instances.push_back(
                        std::move(instance));
                }
            }
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
    normalize_systemverilog_generate_names(unit);
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
    instantiate_checkers(unit);
    resolve_assertion_references(unit);
    resolve_dpi_declarations(
        unit.systemverilog_dpi_declarations,
        unit.functions,
        unit.tasks);
    pending_systemverilog_fsm_pragmas_.clear();
    pending_systemverilog_fsm_pragma_target_position_.reset();
    program_generate_context_ = previous_program_generate_context;
    unit.span = span_from(start, previous());
    return unit;
}

void VerilogParser::parse_anonymous_program(
    ParsedDesign& design, const Token& start)
{
    expect(
        TokenKind::Semicolon,
        "';' after anonymous program",
        "FSIM-SV-PARSE-382");
    const auto duplicate_callable = [&](const std::string_view name) {
        return std::ranges::any_of(
                   design.functions,
                   [&](const FunctionDeclaration& function) {
                       return function.name == name;
                   })
            || std::ranges::any_of(
                design.tasks,
                [&](const TaskDeclaration& task) {
                    return task.name == name;
                });
    };
    while (!at_end() && !keyword("endprogram")) {
        if (match(TokenKind::Semicolon)) {
            continue;
        }
        if (match_keyword("function")) {
            const auto declaration = previous();
            auto function = parse_function(declaration);
            if (duplicate_callable(function.name)) {
                error(
                    declaration,
                    "FSIM-SV-SEM-267",
                    "duplicate anonymous-program callable '"
                        + function.name + "'");
            } else {
                design.functions.push_back(std::move(function));
            }
            continue;
        }
        if (match_keyword("task")) {
            const auto declaration = previous();
            auto task = parse_task(declaration);
            if (duplicate_callable(task.name)) {
                error(
                    declaration,
                    "FSIM-SV-SEM-267",
                    "duplicate anonymous-program callable '"
                        + task.name + "'");
            } else {
                design.tasks.push_back(std::move(task));
            }
            continue;
        }
        if (match_keyword("interface")) {
            const auto declaration = previous();
            if (!match_keyword("class")) {
                error(
                    declaration,
                    "FSIM-SV-PARSE-383",
                    "an anonymous-program interface item must declare an "
                    "interface class");
                skip_to_semicolon();
                continue;
            }
            (void)require_standard(
                "an anonymous-program interface class",
                StandardRevision::SystemVerilog2023,
                declaration,
                "FSIM-SV-PARSE-381");
            add_class_declaration(
                design.systemverilog_classes,
                parse_class(previous(), "$unit", false, true),
                declaration);
            continue;
        }
        if (match_keyword("class")) {
            const auto declaration = previous();
            add_class_declaration(
                design.systemverilog_classes,
                parse_class(declaration, "$unit"),
                declaration);
            continue;
        }
        const auto unsupported = advance();
        error(
            unsupported,
            "FSIM-SV-UNSUPPORTED-025",
            "unsupported anonymous-program item starting with '"
                + unsupported.text + "'");
        skip_to_semicolon();
    }
    expect_keyword("endprogram", false, "FSIM-SV-PARSE-384");
    if (match(TokenKind::Colon)) {
        error(
            previous(),
            "FSIM-SV-SEM-268",
            "an anonymous program cannot have an end name");
        (void)expect_identifier("anonymous-program end name");
    }
    (void)start;
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
