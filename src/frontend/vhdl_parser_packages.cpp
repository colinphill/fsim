// SPDX-License-Identifier: Apache-2.0
#include "vhdl_parser_internal.hpp"

namespace fsim::frontend {

VhdlStandard vhdl_simulator_api_standard(const VhdlSimulatorApi api) noexcept {
  return api == VhdlSimulatorApi::stop || api == VhdlSimulatorApi::finish ||
                 api == VhdlSimulatorApi::resolution_limit
             ? VhdlStandard::Vhdl2008
             : VhdlStandard::Vhdl2019;
}

VhdlSimulatorApi vhdl_simulator_api(
    const std::string_view selected_name) noexcept {
  if (selected_name == "std.env.stop") {
    return VhdlSimulatorApi::stop;
  }
  if (selected_name == "std.env.finish") {
    return VhdlSimulatorApi::finish;
  }
  if (selected_name == "std.env.resolution_limit") {
    return VhdlSimulatorApi::resolution_limit;
  }
  if (selected_name == "std.env.dayofweek") {
    return VhdlSimulatorApi::dayofweek;
  }
  if (selected_name == "std.env.time_record") {
    return VhdlSimulatorApi::time_record;
  }
  if (selected_name == "std.env.localtime") {
    return VhdlSimulatorApi::localtime;
  }
  if (selected_name == "std.env.gmtime") {
    return VhdlSimulatorApi::gmtime;
  }
  if (selected_name == "std.env.epoch") {
    return VhdlSimulatorApi::epoch;
  }
  if (selected_name == "std.env.time_to_seconds") {
    return VhdlSimulatorApi::time_to_seconds;
  }
  if (selected_name == "std.env.seconds_to_time") {
    return VhdlSimulatorApi::seconds_to_time;
  }
  if (selected_name == "std.env.to_string") {
    return VhdlSimulatorApi::to_string;
  }
  if (selected_name == "std.env.getenv") {
    return VhdlSimulatorApi::getenv;
  }
  if (selected_name == "std.env.vhdl_version") {
    return VhdlSimulatorApi::vhdl_version;
  }
  if (selected_name == "std.env.tool_type") {
    return VhdlSimulatorApi::tool_type;
  }
  if (selected_name == "std.env.tool_vendor") {
    return VhdlSimulatorApi::tool_vendor;
  }
  if (selected_name == "std.env.tool_name") {
    return VhdlSimulatorApi::tool_name;
  }
  if (selected_name == "std.env.tool_edition") {
    return VhdlSimulatorApi::tool_edition;
  }
  if (selected_name == "std.env.tool_version") {
    return VhdlSimulatorApi::tool_version;
  }
  if (selected_name == "std.env.call_path_element") {
    return VhdlSimulatorApi::call_path_element;
  }
  if (selected_name == "std.env.call_path_vector") {
    return VhdlSimulatorApi::call_path_vector;
  }
  if (selected_name == "std.env.call_path_vector_ptr") {
    return VhdlSimulatorApi::call_path_vector_ptr;
  }
  if (selected_name == "std.env.get_call_path") {
    return VhdlSimulatorApi::get_call_path;
  }
  if (selected_name == "std.env.file_name") {
    return VhdlSimulatorApi::file_name;
  }
  if (selected_name == "std.env.file_path") {
    return VhdlSimulatorApi::file_path;
  }
  if (selected_name == "std.env.file_line") {
    return VhdlSimulatorApi::file_line;
  }
  if (selected_name == "std.env.directory_items") {
    return VhdlSimulatorApi::directory_items;
  }
  if (selected_name == "std.env.directory") {
    return VhdlSimulatorApi::directory;
  }
  if (selected_name == "std.env.dir_open_status") {
    return VhdlSimulatorApi::dir_open_status;
  }
  if (selected_name == "std.env.dir_create_status") {
    return VhdlSimulatorApi::dir_create_status;
  }
  if (selected_name == "std.env.dir_delete_status") {
    return VhdlSimulatorApi::dir_delete_status;
  }
  if (selected_name == "std.env.file_delete_status") {
    return VhdlSimulatorApi::file_delete_status;
  }
  if (selected_name == "std.env.dir_open") {
    return VhdlSimulatorApi::dir_open;
  }
  if (selected_name == "std.env.dir_close") {
    return VhdlSimulatorApi::dir_close;
  }
  if (selected_name == "std.env.dir_itemexists") {
    return VhdlSimulatorApi::dir_itemexists;
  }
  if (selected_name == "std.env.dir_itemisdir") {
    return VhdlSimulatorApi::dir_itemisdir;
  }
  if (selected_name == "std.env.dir_itemisfile") {
    return VhdlSimulatorApi::dir_itemisfile;
  }
  if (selected_name == "std.env.dir_workingdir") {
    return VhdlSimulatorApi::dir_workingdir;
  }
  if (selected_name == "std.env.dir_createdir") {
    return VhdlSimulatorApi::dir_createdir;
  }
  if (selected_name == "std.env.dir_deletedir") {
    return VhdlSimulatorApi::dir_deletedir;
  }
  if (selected_name == "std.env.dir_deletefile") {
    return VhdlSimulatorApi::dir_deletefile;
  }
  if (selected_name == "std.env.dir_separator") {
    return VhdlSimulatorApi::dir_separator;
  }
  if (selected_name == "std.env.pslassertfailed") {
    return VhdlSimulatorApi::psl_assert_failed;
  }
  if (selected_name == "std.env.psliscovered") {
    return VhdlSimulatorApi::psl_is_covered;
  }
  if (selected_name == "std.env.getpslcoverassert") {
    return VhdlSimulatorApi::get_psl_cover_assert;
  }
  if (selected_name == "std.env.pslisassertcovered") {
    return VhdlSimulatorApi::psl_is_assert_covered;
  }
  if (selected_name == "std.env.setpslcoverassert") {
    return VhdlSimulatorApi::set_psl_cover_assert;
  }
  if (selected_name == "std.env.clearpslstate") {
    return VhdlSimulatorApi::clear_psl_state;
  }
  if (selected_name == "std.env.isvhdlassertfailed") {
    return VhdlSimulatorApi::is_vhdl_assert_failed;
  }
  if (selected_name == "std.env.getvhdlassertcount") {
    return VhdlSimulatorApi::get_vhdl_assert_count;
  }
  if (selected_name == "std.env.clearvhdlassert") {
    return VhdlSimulatorApi::clear_vhdl_assert;
  }
  if (selected_name == "std.env.setvhdlassertenable") {
    return VhdlSimulatorApi::set_vhdl_assert_enable;
  }
  if (selected_name == "std.env.getvhdlassertenable") {
    return VhdlSimulatorApi::get_vhdl_assert_enable;
  }
  if (selected_name == "std.env.setvhdlassertformat") {
    return VhdlSimulatorApi::set_vhdl_assert_format;
  }
  if (selected_name == "std.env.getvhdlassertformat") {
    return VhdlSimulatorApi::get_vhdl_assert_format;
  }
  if (selected_name == "std.env.setvhdlreadseverity") {
    return VhdlSimulatorApi::set_vhdl_read_severity;
  }
  if (selected_name == "std.env.getvhdlreadseverity") {
    return VhdlSimulatorApi::get_vhdl_read_severity;
  }
  return VhdlSimulatorApi::none;
}

Type vhdl_environment_dayofweek_type() {
  Type type;
  type.domain = ValueDomain::Bit2;
  type.spelling = "std.env.dayofweek";
  type.nominal_type = "@builtin:std.env.dayofweek";
  type.vhdl_type_declaration = type.nominal_type;
  type.packed_range = PackedRange{2, 0, true};
  type.enumeration_literals = {
      "sunday", "monday", "tuesday", "wednesday", "thursday", "friday",
      "saturday"};
  type.enumeration_range = EnumerationRange{0, 6, false};
  return type;
}

Type vhdl_environment_time_record_type() {
  auto integer = vhdl_predefined_integer_type(
      VhdlStandard::Vhdl2019, "integer");
  auto weekday = vhdl_environment_dayofweek_type();

  Type type;
  type.domain = ValueDomain::Bit2;
  type.spelling = "std.env.time_record";
  type.nominal_type = "@builtin:std.env.time_record";
  type.vhdl_type_declaration = type.nominal_type;
  type.packed_aggregate = PackedAggregateKind::Struct;
  const auto member = [](std::string name, const Type& subtype,
                          const std::uint64_t offset) {
    return PackedMember{std::move(name), subtype.domain, subtype.spelling,
                        subtype.packed_range, subtype.is_signed,
                        subtype.packed_range_expression, offset, {},
                        std::vector<Type>{subtype}, std::nullopt};
  };
  type.packed_members = {
      member("microsecond", integer, 451),
      member("second", integer, 387),
      member("minute", integer, 323),
      member("hour", integer, 259),
      member("day", integer, 195),
      member("month", integer, 131),
      member("year", integer, 67),
      member("weekday", weekday, 64),
      member("dayofyear", integer, 0),
  };
  type.packed_range = PackedRange{514, 0, true};
  return type;
}

bool is_vhdl_environment_time_record(const Type& type) noexcept {
  return type.nominal_type == "@builtin:std.env.time_record";
}

std::optional<Type> vhdl_reflection_type(const std::string_view simple_name) {
  if (simple_name == "type_class" || simple_name == "value_class") {
    Type type;
    type.domain = ValueDomain::Bit2;
    type.spelling = "std.reflection." + std::string { simple_name };
    type.nominal_type = "@builtin:std.reflection.type_class";
    type.vhdl_type_declaration = type.nominal_type;
    type.packed_range = PackedRange { 3, 0, true };
    type.enumeration_literals = { "class_enumeration", "class_integer",
        "class_floating", "class_physical", "class_record", "class_array",
        "class_access", "class_file", "class_protected" };
    type.enumeration_range = EnumerationRange { 0, 8, false };
    return type;
  }
  constexpr std::array mirror_names {
    std::string_view { "value_mirror" },
    std::string_view { "subtype_mirror" },
    std::string_view { "enumeration_value_mirror" },
    std::string_view { "enumeration_subtype_mirror" },
    std::string_view { "integer_value_mirror" },
    std::string_view { "integer_subtype_mirror" },
    std::string_view { "floating_value_mirror" },
    std::string_view { "floating_subtype_mirror" },
    std::string_view { "physical_value_mirror" },
    std::string_view { "physical_subtype_mirror" },
    std::string_view { "record_value_mirror" },
    std::string_view { "record_subtype_mirror" },
    std::string_view { "array_value_mirror" },
    std::string_view { "array_subtype_mirror" },
    std::string_view { "access_value_mirror" },
    std::string_view { "access_subtype_mirror" },
    std::string_view { "file_value_mirror" },
    std::string_view { "file_subtype_mirror" },
    std::string_view { "protected_value_mirror" },
    std::string_view { "protected_subtype_mirror" }
  };
  if (std::ranges::find(mirror_names, simple_name) == mirror_names.end()) {
    return std::nullopt;
  }
  Type type;
  type.domain = ValueDomain::Bit2;
  type.spelling = "std.reflection." + std::string { simple_name };
  type.nominal_type = "@builtin:" + type.spelling;
  type.vhdl_type_declaration = type.nominal_type;
  type.packed_range = PackedRange { 31, 0, true };
  return type;
}

namespace {

Type vhdl_environment_string_container_type(
    const std::string_view spelling,
    const std::string_view nominal_type) {
  Type element;
  element.domain = ValueDomain::String;
  element.spelling = "line";
  element.nominal_type = "@builtin:std.textio.line";
  element.vhdl_type_declaration = element.nominal_type;

  Type type = element;
  type.spelling = std::string{spelling};
  type.nominal_type = std::string{nominal_type};
  type.vhdl_type_declaration = type.nominal_type;
  SystemVerilogContainerInfo container;
  container.kind = SystemVerilogContainerKind::DynamicArray;
  container.element_types.push_back(std::move(element));
  type.systemverilog_container = std::move(container);
  return type;
}

}  // namespace

Type vhdl_environment_directory_type() {
  return vhdl_environment_string_container_type(
      "std.env.directory", "@builtin:std.env.directory");
}

Type vhdl_environment_directory_items_type() {
  return vhdl_environment_string_container_type(
      "std.env.directory_items", "@builtin:std.env.directory_items");
}

Type vhdl_environment_directory_status_type(const VhdlSimulatorApi kind) {
  Type type;
  type.domain = ValueDomain::Bit2;
  type.is_signed = false;
  type.packed_range = PackedRange{2, 0, true};
  switch (kind) {
  case VhdlSimulatorApi::dir_open_status:
    type.spelling = "std.env.dir_open_status";
    type.enumeration_literals = {"status_ok", "status_not_found",
        "status_no_directory", "status_access_denied", "status_error"};
    break;
  case VhdlSimulatorApi::dir_create_status:
    type.spelling = "std.env.dir_create_status";
    type.enumeration_literals = {"status_ok", "status_item_exists",
        "status_access_denied", "status_error"};
    break;
  case VhdlSimulatorApi::dir_delete_status:
    type.spelling = "std.env.dir_delete_status";
    type.enumeration_literals = {"status_ok", "status_no_directory",
        "status_not_empty", "status_access_denied", "status_error"};
    break;
  case VhdlSimulatorApi::file_delete_status:
    type.spelling = "std.env.file_delete_status";
    type.enumeration_literals = {"status_ok", "status_no_file",
        "status_access_denied", "status_error"};
    break;
  default:
    type.spelling = "std.env.invalid_status";
    break;
  }
  type.nominal_type = "@builtin:" + type.spelling;
  type.vhdl_type_declaration = type.nominal_type;
  type.enumeration_range = EnumerationRange{
      0, static_cast<std::int64_t>(type.enumeration_literals.size() - 1U),
      false};
  return type;
}

bool is_vhdl_environment_directory(const Type& type) noexcept {
  return type.nominal_type == "@builtin:std.env.directory";
}

Type vhdl_environment_call_path_element_type() {
  Type line;
  line.domain = ValueDomain::String;
  line.spelling = "std.textio.line";
  line.nominal_type = "@builtin:std.textio.line";
  line.vhdl_type_declaration = line.nominal_type;
  auto positive = vhdl_predefined_integer_type(
      VhdlStandard::Vhdl2019, "positive");
  positive.integer_range = IntegerRange{
      1, std::numeric_limits<std::int64_t>::max(), false};

  Type type;
  type.domain = ValueDomain::Unknown;
  type.spelling = "std.env.call_path_element";
  type.nominal_type = "@builtin:std.env.call_path_element";
  type.vhdl_type_declaration = type.nominal_type;
  type.packed_aggregate = PackedAggregateKind::UnpackedStruct;
  const auto member = [](std::string name, const Type& subtype) {
    return PackedMember{std::move(name), subtype.domain, subtype.spelling,
        subtype.packed_range, subtype.is_signed,
        subtype.packed_range_expression, 0, {},
        std::vector<Type>{subtype}, std::nullopt};
  };
  type.packed_members = {
      member("name", line),
      member("file_name", line),
      member("file_path", line),
      member("file_line", positive),
  };
  return type;
}

namespace {

Type vhdl_environment_call_path_container_type(
    const std::string_view spelling,
    const std::string_view nominal_type) {
  auto element = vhdl_environment_call_path_element_type();
  Type type = element;
  type.spelling = std::string{spelling};
  type.nominal_type = std::string{nominal_type};
  type.vhdl_type_declaration = type.nominal_type;
  SystemVerilogContainerInfo container;
  container.kind = SystemVerilogContainerKind::DynamicArray;
  container.element_types.push_back(std::move(element));
  type.systemverilog_container = std::move(container);
  return type;
}


}  // namespace

Type vhdl_environment_call_path_vector_type() {
  return vhdl_environment_call_path_container_type(
      "std.env.call_path_vector", "@builtin:std.env.call_path_vector");
}

Type vhdl_environment_call_path_vector_ptr_type() {
  return vhdl_environment_call_path_container_type(
      "std.env.call_path_vector_ptr",
      "@builtin:std.env.call_path_vector_ptr");
}

bool is_vhdl_environment_call_path_type(const Type& type) noexcept {
  return type.nominal_type == "@builtin:std.env.call_path_element"
      || type.nominal_type == "@builtin:std.env.call_path_vector"
      || type.nominal_type == "@builtin:std.env.call_path_vector_ptr";
}

std::string VhdlParser::parse_vhdl_selected_name(
    const std::string_view description,
    std::string* const source_spelling) {
  const auto first = expect_identifier(description);
  std::string result = vhdl_name(first.text);
  std::string spelling = first.text;
  while (match(TokenKind::Dot)) {
    const auto part = expect_identifier(description);
    result += '.';
    result += vhdl_name(part.text);
    spelling += '.';
    spelling += part.text;
  }
  if (source_spelling != nullptr) {
    *source_spelling = std::move(spelling);
  }
  return result;
}

void VhdlParser::parse_vhdl_package_generic_map(
    std::vector<ParameterOverride>& associations,
    bool& box,
    const Token& start) {
  expect_keyword("map", true, "FSIM-VHDL-PARSE-181");
  expect(
      TokenKind::LeftParen,
      "'(' after package generic map",
      "FSIM-VHDL-PARSE-182");
  if (match(TokenKind::Less)) {
    expect(
        TokenKind::Greater,
        "'>' in package generic box",
        "FSIM-VHDL-PARSE-183");
    box = true;
    expect(
        TokenKind::RightParen,
        "')' after package generic box",
        "FSIM-VHDL-PARSE-184");
    return;
  }

  bool saw_named = false;
  const auto begins_unambiguous_subtype_indication = [&]() {
    if (!at(TokenKind::Identifier)) {
      return false;
    }
    std::size_t lookahead = 1;
    while (at(TokenKind::Dot, lookahead)
           && at(TokenKind::Identifier, lookahead + 1)) {
      lookahead += 2;
    }
    return keyword("range", lookahead, true);
  };
  while (!at_end() && !at(TokenKind::RightParen)) {
    const auto association_start = current();
    ParameterOverride actual;
    if (at(TokenKind::Identifier)
        && at(TokenKind::Arrow, 1)) {
      saw_named = true;
      const auto name = advance();
      advance();
      actual.name = vhdl_name(name.text);
      if (std::ranges::any_of(
              associations,
              [&](const ParameterOverride& existing) {
                return existing.name == actual.name;
              })) {
        error(
            name,
            "FSIM-VHDL-SEM-057",
            "duplicate package generic association '"
                + *actual.name + "'");
      }
    } else if (saw_named) {
      error(
          current(),
          "FSIM-VHDL-SEM-058",
          "a positional package generic association cannot follow a "
          "named association");
    }

    if (match(TokenKind::Less)) {
      expect(
          TokenKind::Greater,
          "'>' in default package generic association",
          "FSIM-VHDL-PARSE-185");
      actual.default_box = true;
    } else if (begins_unambiguous_subtype_indication()) {
      actual.type_value = parse_vhdl_type(true, true);
    } else {
      actual.value = parse_expression();
    }
    actual.span =
        cover(association_start.span, previous().span);
    associations.push_back(std::move(actual));
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(
      TokenKind::RightParen,
      "')' after package generic associations",
      "FSIM-VHDL-PARSE-186");
  (void)start;
}

ParameterDeclaration VhdlParser::parse_vhdl_interface_package(
    const Token& start) {
  const auto name =
      expect_identifier("interface package generic name");
  expect_keyword("is", true, "FSIM-VHDL-PARSE-187");
  expect_keyword("new", true, "FSIM-VHDL-PARSE-188");

  InterfacePackageProfile profile;
  profile.template_name =
      parse_vhdl_selected_name("generic package template name");
  const auto generic =
      expect_keyword("generic", true, "FSIM-VHDL-PARSE-189");
  parse_vhdl_package_generic_map(
      profile.generic_map, profile.generic_map_box, generic);
  profile.span = span_from(start, previous());

  ParameterDeclaration result;
  result.name = vhdl_name(name.text);
  result.span = profile.span;
  result.kind = ParameterKind::Package;
  result.package_profile = std::move(profile);
  return result;
}

PackageInstantiation VhdlParser::parse_vhdl_package_instantiation(
    const Token& start) {
    require_vhdl_standard(
        start, VhdlStandard::Vhdl2008, "a local package instantiation",
        "select VHDL-2008 or use a non-generic package declaration");
    PackageInstantiation result;
    const auto name = expect_identifier("local package instance name");
    result.name = vhdl_name(name.text);
    expect_keyword("is", true, "FSIM-VHDL-PARSE-190");
    expect_keyword("new", true, "FSIM-VHDL-PARSE-191");
    result.template_name = parse_vhdl_selected_name("generic package template name");
    const auto generic = expect_keyword("generic", true, "FSIM-VHDL-PARSE-192");
    parse_vhdl_package_generic_map(
        result.generic_map, result.generic_map_box, generic);
    expect(
        TokenKind::Semicolon,
        "';' after local package instantiation",
        "FSIM-VHDL-PARSE-193");
    result.span = span_from(start, previous());
    return result;
}

bool VhdlParser::expand_vhdl_library_package_instance() {
  auto& tokens = tokens_;
  const auto start = position();
  const auto word = [&](const std::size_t index, const std::string_view text) {
    return index < tokens.size()
        && tokens[index].kind == TokenKind::Identifier
        && detail::ascii_lower(tokens[index].text) == text;
  };
  const auto kind = [&](const std::size_t index, const TokenKind expected) {
    return index < tokens.size() && tokens[index].kind == expected;
  };
  if (!word(start, "package") || !kind(start + 1U, TokenKind::Identifier)
      || !word(start + 2U, "is") || !word(start + 3U, "new")) {
    return false;
  }
  const auto& instance_token = tokens[start + 1U];
  // Template name: [library.]name.
  auto index = start + 4U;
  if (!kind(index, TokenKind::Identifier)) {
    return false;
  }
  std::string template_name = detail::ascii_lower(tokens[index].text);
  ++index;
  while (kind(index, TokenKind::Dot) && kind(index + 1U, TokenKind::Identifier)) {
    template_name = detail::ascii_lower(tokens[index + 1U].text);
    index += 2U;
  }
  // Generic map associations as token ranges.
  struct Association {
    std::string formal;
    std::vector<Token> actual;
  };
  std::vector<Association> associations;
  if (word(index, "generic") && word(index + 1U, "map")
      && kind(index + 2U, TokenKind::LeftParen)) {
    index += 3U;
    std::size_t depth { };
    Association current;
    for (; index < tokens.size(); ++index) {
      const auto& token = tokens[index];
      if (token.kind == TokenKind::LeftParen) {
        ++depth;
      } else if (token.kind == TokenKind::RightParen) {
        if (depth == 0U) {
          break;
        }
        --depth;
      } else if (depth == 0U && token.kind == TokenKind::Comma) {
        associations.push_back(std::move(current));
        current = { };
        continue;
      } else if (depth == 0U && token.kind == TokenKind::Arrow
          && current.formal.empty() && current.actual.size() == 1U
          && current.actual.front().kind == TokenKind::Identifier) {
        current.formal = detail::ascii_lower(current.actual.front().text);
        current.actual.clear();
        continue;
      }
      current.actual.push_back(token);
    }
    if (!kind(index, TokenKind::RightParen)) {
      return false;
    }
    associations.push_back(std::move(current));
    ++index;
  }
  if (!kind(index, TokenKind::Semicolon)) {
    return false;
  }
  const auto end = index;
  for (const auto& association : associations) {
    if (association.actual.empty()
        || (association.actual.size() == 1U
            && association.actual.front().kind == TokenKind::Less)) {
      return false;
    }
  }

  // The end of a unit begun at `from`: an `end` at nesting depth zero that
  // is followed by `;`, the unit kind, or the unit name.
  const auto unit_end = [&](std::size_t from, const std::string_view name)
      -> std::optional<std::size_t> {
    std::size_t depth { };
    for (auto cursor = from; cursor < tokens.size(); ++cursor) {
      if (word(cursor, "begin")) {
        ++depth;
        continue;
      }
      if (!word(cursor, "end")) {
        continue;
      }
      const auto next = cursor + 1U;
      const bool construct_end = word(next, "if") || word(next, "loop")
          || word(next, "case") || word(next, "record")
          || word(next, "units") || word(next, "protected")
          || word(next, "component") || word(next, "generate")
          || word(next, "block") || word(next, "process")
          || word(next, "for") || word(next, "while");
      if (construct_end) {
        continue;
      }
      if (depth > 0U) {
        --depth;
        continue;
      }
      if (kind(next, TokenKind::Semicolon) || word(next, "package")
          || word(next, name)) {
        auto semicolon = next;
        while (semicolon < tokens.size()
            && !kind(semicolon, TokenKind::Semicolon)) {
          ++semicolon;
        }
        return semicolon < tokens.size() ? std::optional { semicolon }
                                         : std::nullopt;
      }
    }
    return std::nullopt;
  };
  // The context clause (library and use clauses) just before a unit.
  const auto context_before = [&](const std::size_t unit_start) {
    auto first = unit_start;
    while (first > 0U && kind(first - 1U, TokenKind::Semicolon)) {
      auto clause = first - 1U;
      while (clause > 0U && !kind(clause - 1U, TokenKind::Semicolon)) {
        --clause;
      }
      if (!word(clause, "library") && !word(clause, "use")) {
        break;
      }
      first = clause;
    }
    return std::vector<Token>(
        tokens.begin() + static_cast<std::ptrdiff_t>(first),
        tokens.begin() + static_cast<std::ptrdiff_t>(unit_start));
  };

  std::optional<std::size_t> declaration;
  std::optional<std::size_t> body;
  for (std::size_t cursor { }; cursor + 3U < tokens.size(); ++cursor) {
    if (!word(cursor, "package")) {
      continue;
    }
    if (word(cursor + 1U, "body") && word(cursor + 2U, template_name)
        && word(cursor + 3U, "is")) {
      body = cursor;
    } else if (word(cursor + 1U, template_name) && word(cursor + 2U, "is")
        && word(cursor + 3U, "generic")) {
      declaration = cursor;
    }
  }
  if (!declaration || !kind(*declaration + 4U, TokenKind::LeftParen)) {
    return false;
  }
  // The generic clause: interface declarations separated by `;`.
  std::vector<std::vector<Token>> interfaces;
  std::size_t clause_end { };
  {
    std::size_t depth { };
    std::vector<Token> current;
    auto cursor = *declaration + 5U;
    for (; cursor < tokens.size(); ++cursor) {
      const auto& token = tokens[cursor];
      if (token.kind == TokenKind::LeftParen) {
        ++depth;
      } else if (token.kind == TokenKind::RightParen) {
        if (depth == 0U) {
          break;
        }
        --depth;
      } else if (depth == 0U && token.kind == TokenKind::Semicolon) {
        interfaces.push_back(std::move(current));
        current.clear();
        continue;
      }
      current.push_back(token);
    }
    if (!kind(cursor, TokenKind::RightParen)
        || !kind(cursor + 1U, TokenKind::Semicolon)) {
      return false;
    }
    if (!current.empty()) {
      interfaces.push_back(std::move(current));
    }
    clause_end = cursor + 1U;
  }
  const auto declaration_end = unit_end(clause_end + 1U, template_name);
  if (!declaration_end) {
    return false;
  }
  // Each generic: its name, whether it is a type, and its subtype and
  // default tokens.
  struct Generic {
    std::string name;
    bool type { };
    std::vector<Token> subtype;
    std::vector<Token> default_value;
    // A subprogram generic: its alias signature `[t1, t2 return t]`.
    std::optional<std::vector<Token>> signature;
    // The subprogram specification and its parameter names, for a
    // wrapper that calls the actual.
    std::vector<Token> header;
    std::vector<Token> parameter_names;
    bool function { };
  };
  std::vector<Generic> generics;
  for (auto interface : interfaces) {
    std::size_t cursor { };
    // `[pure|impure] function f (a, b : t) return r [is name|<>]` and
    // `procedure p (a : t)` become aliases with a signature.
    if (!interface.empty() && interface.front().kind == TokenKind::Identifier
        && (detail::ascii_lower(interface.front().text) == "pure"
            || detail::ascii_lower(interface.front().text) == "impure")) {
      ++cursor;
    }
    if (cursor + 1U < interface.size()
        && interface[cursor].kind == TokenKind::Identifier
        && (detail::ascii_lower(interface[cursor].text) == "function"
            || detail::ascii_lower(interface[cursor].text) == "procedure")
        && (interface[cursor + 1U].kind == TokenKind::Identifier
            || interface[cursor + 1U].kind == TokenKind::StringLiteral)) {
      Generic subprogram;
      subprogram.name = interface[cursor + 1U].kind == TokenKind::Identifier
          ? detail::ascii_lower(interface[cursor + 1U].text)
          : interface[cursor + 1U].text;
      auto signature = std::vector<Token> { };
      auto position = cursor + 2U;
      if (position < interface.size()
          && interface[position].kind == TokenKind::LeftParen) {
        // Each parameter declaration contributes its type once per name.
        std::size_t depth { };
        std::vector<Token> parameter;
        const auto flush = [&] {
          const auto colon = std::ranges::find_if(parameter,
              [](const Token& token) { return token.kind == TokenKind::Colon; });
          if (colon == parameter.end()) {
            return false;
          }
          std::size_t names = 1U;
          for (auto it = parameter.begin(); it != colon; ++it) {
            names += it->kind == TokenKind::Comma ? 1U : 0U;
            if (it->kind == TokenKind::Identifier
                && detail::ascii_lower(it->text) != "constant"
                && detail::ascii_lower(it->text) != "signal"
                && detail::ascii_lower(it->text) != "variable") {
              subprogram.parameter_names.push_back(*it);
            }
          }
          auto type_begin = std::next(colon);
          while (type_begin != parameter.end()
              && type_begin->kind == TokenKind::Identifier
              && (detail::ascii_lower(type_begin->text) == "in"
                  || detail::ascii_lower(type_begin->text) == "out"
                  || detail::ascii_lower(type_begin->text) == "inout")) {
            ++type_begin;
          }
          auto type_end = std::ranges::find_if(type_begin, parameter.end(),
              [](const Token& token) {
                return token.kind == TokenKind::ColonEqual;
              });
          if (type_begin == type_end) {
            return false;
          }
          for (std::size_t name { }; name < names; ++name) {
            if (!signature.empty()) {
              signature.push_back(Token { TokenKind::Comma, ",",
                  type_begin->span, { }, { } });
            }
            signature.insert(signature.end(), type_begin, type_end);
          }
          parameter.clear();
          return true;
        };
        for (++position; position < interface.size(); ++position) {
          const auto& token = interface[position];
          if (token.kind == TokenKind::LeftParen) {
            ++depth;
          } else if (token.kind == TokenKind::RightParen) {
            if (depth == 0U) {
              break;
            }
            --depth;
          } else if (depth == 0U && token.kind == TokenKind::Semicolon) {
            if (!flush()) {
              return false;
            }
            continue;
          }
          parameter.push_back(token);
        }
        if (position >= interface.size() || !flush()) {
          return false;
        }
        ++position;
      }
      if (position + 1U < interface.size()
          && interface[position].kind == TokenKind::Identifier
          && detail::ascii_lower(interface[position].text) == "return") {
        signature.push_back(Token { TokenKind::Identifier, "return",
            interface[position].span, { }, { } });
        signature.push_back(interface[position + 1U]);
        position += 2U;
      }
      subprogram.function = detail::ascii_lower(interface[cursor].text)
          == "function";
      subprogram.header.assign(interface.begin(),
          interface.begin() + static_cast<std::ptrdiff_t>(position));
      if (position + 1U < interface.size()
          && interface[position].kind == TokenKind::Identifier
          && detail::ascii_lower(interface[position].text) == "is"
          && interface[position + 1U].kind != TokenKind::Less) {
        subprogram.default_value.assign(
            interface.begin() + static_cast<std::ptrdiff_t>(position + 1U),
            interface.end());
      }
      subprogram.signature = std::move(signature);
      generics.push_back(std::move(subprogram));
      continue;
    }
    cursor = 0U;
    if (!interface.empty() && interface.front().kind == TokenKind::Identifier
        && detail::ascii_lower(interface.front().text) == "constant") {
      ++cursor;
    }
    if (cursor < interface.size()
        && interface[cursor].kind == TokenKind::Identifier
        && detail::ascii_lower(interface[cursor].text) == "type"
        && cursor + 1U < interface.size()) {
      if (cursor + 2U != interface.size()) {
        return false;
      }
      generics.push_back({ detail::ascii_lower(interface[cursor + 1U].text),
          true, { }, { }, std::nullopt, { }, { }, false });
      continue;
    }
    std::vector<std::string> names;
    while (cursor < interface.size()
        && interface[cursor].kind == TokenKind::Identifier) {
      const auto lowered = detail::ascii_lower(interface[cursor].text);
      if (lowered == "function" || lowered == "procedure"
          || lowered == "package" || lowered == "signal"
          || lowered == "variable" || lowered == "file") {
        return false;
      }
      names.push_back(lowered);
      ++cursor;
      if (cursor < interface.size()
          && interface[cursor].kind == TokenKind::Comma) {
        ++cursor;
        continue;
      }
      break;
    }
    if (names.empty() || cursor >= interface.size()
        || interface[cursor].kind != TokenKind::Colon) {
      return false;
    }
    ++cursor;
    if (cursor < interface.size()
        && interface[cursor].kind == TokenKind::Identifier
        && detail::ascii_lower(interface[cursor].text) == "in") {
      ++cursor;
    }
    std::vector<Token> subtype;
    std::vector<Token> default_value;
    for (; cursor < interface.size(); ++cursor) {
      if (interface[cursor].kind == TokenKind::ColonEqual) {
        default_value.assign(interface.begin()
                + static_cast<std::ptrdiff_t>(cursor + 1U),
            interface.end());
        break;
      }
      subtype.push_back(interface[cursor]);
    }
    if (subtype.empty()) {
      return false;
    }
    for (const auto& name : names) {
      generics.push_back(
          { name, false, subtype, default_value, std::nullopt, { }, { },
            false });
    }
  }
  // Actuals: positional in order, then named.
  std::vector<std::optional<std::vector<Token>>> actuals(generics.size());
  std::size_t positional { };
  for (const auto& association : associations) {
    if (association.formal.empty()) {
      if (positional >= generics.size()) {
        return false;
      }
      actuals[positional++] = association.actual;
      continue;
    }
    const auto found = std::ranges::find(generics, association.formal,
        &Generic::name);
    if (found == generics.end()) {
      return false;
    }
    const auto at = static_cast<std::size_t>(
        std::distance(generics.begin(), found));
    if (association.actual.size() == 1U
        && association.actual.front().kind == TokenKind::Identifier
        && detail::ascii_lower(association.actual.front().text) == "open") {
      continue;
    }
    actuals[at] = association.actual;
  }

  const auto span = instance_token.span;
  const auto make = [&](const TokenKind token_kind, std::string text) {
    return Token { token_kind, std::move(text), span, { }, { } };
  };
  const auto identifier = [&](std::string text) {
    return make(TokenKind::Identifier, std::move(text));
  };
  const auto& instance_name = instance_token.text;
  std::vector<Token> wrappers;
  std::vector<Token> expanded = context_before(*declaration);
  expanded.push_back(identifier("package"));
  expanded.push_back(identifier(instance_name));
  expanded.push_back(identifier("is"));
  for (std::size_t generic { }; generic < generics.size(); ++generic) {
    const auto& formal = generics[generic];
    const auto& value = actuals[generic] ? *actuals[generic]
                                         : formal.default_value;
    if (value.empty()) {
      return false;
    }
    if (formal.signature) {
      // A wrapper subprogram that calls the actual; its body follows in the
      // package body.
      expanded.insert(expanded.end(), formal.header.begin(),
          formal.header.end());
      std::vector<Token> wrapper = formal.header;
      wrapper.push_back(identifier("is"));
      wrapper.push_back(identifier("begin"));
      if (formal.function) {
        wrapper.push_back(identifier("return"));
      }
      wrapper.insert(wrapper.end(), value.begin(), value.end());
      if (!formal.parameter_names.empty()) {
        wrapper.push_back(make(TokenKind::LeftParen, "("));
        for (std::size_t name { }; name < formal.parameter_names.size();
             ++name) {
          if (name != 0U) {
            wrapper.push_back(make(TokenKind::Comma, ","));
          }
          wrapper.push_back(formal.parameter_names[name]);
        }
        wrapper.push_back(make(TokenKind::RightParen, ")"));
      }
      wrapper.push_back(make(TokenKind::Semicolon, ";"));
      wrapper.push_back(identifier("end"));
      wrapper.push_back(make(TokenKind::Semicolon, ";"));
      wrappers.insert(wrappers.end(), wrapper.begin(), wrapper.end());
    } else if (formal.type) {
      expanded.push_back(identifier("subtype"));
      expanded.push_back(identifier(formal.name));
      expanded.push_back(identifier("is"));
      expanded.insert(expanded.end(), value.begin(), value.end());
    } else {
      expanded.push_back(identifier("constant"));
      expanded.push_back(identifier(formal.name));
      expanded.push_back(make(TokenKind::Colon, ":"));
      expanded.insert(expanded.end(), formal.subtype.begin(),
          formal.subtype.end());
      expanded.push_back(make(TokenKind::ColonEqual, ":="));
      expanded.insert(expanded.end(), value.begin(), value.end());
    }
    expanded.push_back(make(TokenKind::Semicolon, ";"));
  }
  auto declaration_end_keyword = *declaration_end;
  while (declaration_end_keyword > clause_end
      && !word(declaration_end_keyword, "end")) {
    --declaration_end_keyword;
  }
  expanded.insert(expanded.end(),
      tokens.begin() + static_cast<std::ptrdiff_t>(clause_end + 1U),
      tokens.begin() + static_cast<std::ptrdiff_t>(declaration_end_keyword));
  // The template's closing `end ... ;` names the instance instead.
  expanded.push_back(identifier("end"));
  expanded.push_back(identifier("package"));
  expanded.push_back(identifier(instance_name));
  expanded.push_back(make(TokenKind::Semicolon, ";"));
  // A subprogram actual named by `lib.pkg.name` is made visible to the
  // wrappers through a use clause of its package.
  {
    std::vector<Token> uses;
    for (std::size_t generic { }; generic < generics.size(); ++generic) {
      if (!generics[generic].signature || !actuals[generic]) {
        continue;
      }
      const auto& value = *actuals[generic];
      if (value.size() != 5U || value[1].kind != TokenKind::Dot
          || value[3].kind != TokenKind::Dot) {
        continue;
      }
      uses.push_back(identifier("use"));
      uses.push_back(value[0]);
      uses.push_back(make(TokenKind::Dot, "."));
      uses.push_back(value[2]);
      uses.push_back(make(TokenKind::Dot, "."));
      uses.push_back(identifier("all"));
      uses.push_back(make(TokenKind::Semicolon, ";"));
    }
    expanded.insert(expanded.begin(), uses.begin(), uses.end());
  }
  if (!body && !wrappers.empty()) {
    expanded.push_back(identifier("package"));
    expanded.push_back(identifier("body"));
    expanded.push_back(identifier(instance_name));
    expanded.push_back(identifier("is"));
    expanded.insert(expanded.end(), wrappers.begin(), wrappers.end());
    expanded.push_back(identifier("end"));
    expanded.push_back(identifier("package"));
    expanded.push_back(identifier("body"));
    expanded.push_back(identifier(instance_name));
    expanded.push_back(make(TokenKind::Semicolon, ";"));
  }
  if (body) {
    const auto body_items = *body + 4U;
    const auto body_end = unit_end(body_items, template_name);
    if (!body_end) {
      return false;
    }
    auto end_keyword = *body_end;
    while (end_keyword > body_items && !word(end_keyword, "end")) {
      --end_keyword;
    }
    const auto context = context_before(*body);
    expanded.insert(expanded.end(), context.begin(), context.end());
    expanded.push_back(identifier("package"));
    expanded.push_back(identifier("body"));
    expanded.push_back(identifier(instance_name));
    expanded.push_back(identifier("is"));
    expanded.insert(expanded.end(), wrappers.begin(), wrappers.end());
    expanded.insert(expanded.end(),
        tokens.begin() + static_cast<std::ptrdiff_t>(body_items),
        tokens.begin() + static_cast<std::ptrdiff_t>(end_keyword));
    expanded.push_back(identifier("end"));
    expanded.push_back(identifier("package"));
    expanded.push_back(identifier("body"));
    expanded.push_back(identifier(instance_name));
    expanded.push_back(make(TokenKind::Semicolon, ";"));
  }
  // Replace the instantiation.
  tokens.erase(tokens.begin() + static_cast<std::ptrdiff_t>(start),
      tokens.begin() + static_cast<std::ptrdiff_t>(end + 1U));
  tokens.insert(tokens.begin() + static_cast<std::ptrdiff_t>(start),
      expanded.begin(), expanded.end());
  return true;
}

} // namespace fsim::frontend
