// SPDX-License-Identifier: Apache-2.0
#include "vhdl_parser_internal.hpp"

namespace fsim::frontend {

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
    const std::string_view description) {
  const auto first = expect_identifier(description);
  std::string result = vhdl_name(first.text);
  while (match(TokenKind::Dot)) {
    const auto part = expect_identifier(description);
    result += '.';
    result += vhdl_name(part.text);
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

} // namespace fsim::frontend
