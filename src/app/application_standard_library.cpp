// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app::application_detail {
namespace {

struct PackageSource {
  std::string_view name;
  std::string_view declaration;
  std::string_view declaration_hash;
  std::string_view body;
  std::string_view body_hash;
};

constexpr std::array kPackages{
    PackageSource{
        "std_logic_1164",
        "ieee/std_logic_1164.vhdl",
        "2a34c7d7b2c8ba21b1e91153741399cf2cd23c8b04028dcf53765efeea76de55",
        "ieee/std_logic_1164-body.vhdl",
        "6534fe4842c1133199db93725e36a9e973ea8e2ab03890433c013af813d5ce2c"},
    PackageSource{
        "std_logic_textio",
        "ieee/std_logic_textio.vhdl",
        "526a2e1e0a05f35ae97fb046ec90ebe8250324390aab2f3adccde73e605e3937",
        "",
        ""},
    PackageSource{
        "numeric_bit",
        "ieee/numeric_bit.vhdl",
        "e54a257a6da6141ef2fb0b51dccbde127a665e474e4e8e354f28e720b5d440be",
        "ieee/numeric_bit-body.vhdl",
        "21fc27ef3d7ff0932ebb6c92a4d5f10865b1867ce392a008c2de1d6fde3e11ab"},
    PackageSource{
        "numeric_std",
        "ieee/numeric_std.vhdl",
        "fcb9b1d05f8d98cd068e464bf20804d432a234128b253218524901bc96d19631",
        "ieee/numeric_std-body.vhdl",
        "10e8bdc4fedc881a972f5900abe833d24397d686e07b566479c47495acf39721"},
    PackageSource{
        "math_real",
        "ieee/math_real.vhdl",
        "33fe4fe3fc21cbe6c36ed4969d96ed25549680bb3d936f106078fe47af2fec7b",
        "ieee/math_real-body.vhdl",
        "ed057e95cd908b547d128d6a29dbfcf243ba64468d6e6cc780090bc9cd79f3b2"},
    PackageSource{
        "fixed_float_types",
        "ieee/fixed_float_types.vhdl",
        "649090aaf4dbb7a6bf88a6720a265aaf3be0360f449eb1462777f1c7f48c249e",
        "",
        ""},
    PackageSource{
        "fixed_generic_pkg",
        "ieee/fixed_generic_pkg.vhdl",
        "76f98d70b4e5e80ee4411f3b669ac44beed112b8ea0d4fb89005a7aedd20ffef",
        "ieee/fixed_generic_pkg-body.vhdl",
        "b5e3731985388b9e396bc7c62949775e7b13241c3b53281262947ef88cb28fad"},
    PackageSource{
        "fixed_pkg",
        "ieee/fixed_pkg.vhdl",
        "bbd601960c294aa0675a91b4576f1fcfff2d612fe911ad8cc51ca2c10b1565cd",
        "",
        ""},
    PackageSource{
        "float_generic_pkg",
        "ieee/float_generic_pkg.vhdl",
        "f6bdde6dcd120358d8ce51b2760bdffaebd2e5a0ab5267fe9f7433eec89451d9",
        "ieee/float_generic_pkg-body.vhdl",
        "093b095a30ca301968d3a3680c995fee5e59cd34b3b93bdd04bfb1e7b00002d4"},
    PackageSource{
        "float_pkg",
        "ieee/float_pkg.vhdl",
        "46e9f26610bd457184960dadb39c7d0e9fd5e15a7662414e5c3e2a79e377e05a",
        "",
        ""},
};

bool uses_package(
    const frontend::ParsedDesign& parsed,
    const std::string_view package) {
  const auto prefix = "ieee." + std::string{package};
  for (const auto& unit : parsed.units) {
    if (unit.language != frontend::Language::Vhdl2008) {
      continue;
    }
    for (const auto& item : unit.vhdl_context) {
      if (item.kind != frontend::VhdlContextItemKind::UseClause) {
        continue;
      }
      for (const auto& name : item.selected_names) {
        if (name == prefix || name.starts_with(prefix + ".")) {
          return true;
        }
      }
    }
  }
  return false;
}

std::filesystem::path library_root() {
  if (const auto override =
          support::environment_variable("FSIM_IEEE_LIBRARY");
      override && !override->empty()) {
    return std::filesystem::path{*override}.lexically_normal();
  }
#if defined(FSIM_IEEE_LIBRARY_SOURCE_DIR)
  return std::filesystem::path{FSIM_IEEE_LIBRARY_SOURCE_DIR}
      .lexically_normal();
#else
  return {};
#endif
}

std::optional<std::string> checked_source_text(
    const std::filesystem::path& path,
    const std::string_view expected,
    diagnostic::Engine& diagnostics) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    diagnostics.error(
        "FSIM-FE-VHSTD-001",
        "bundled IEEE 1076-2019 source is unavailable: "
            + path.generic_string());
    return std::nullopt;
  }
  std::string text{
      std::istreambuf_iterator<char>(stream),
      std::istreambuf_iterator<char>()};
  if (!stream.good() && !stream.eof()) {
    diagnostics.error(
        "FSIM-FE-VHSTD-001",
        "bundled IEEE 1076-2019 source could not be read: "
            + path.generic_string());
    return std::nullopt;
  }
  const auto digest = support::Sha256::hex(
      support::Sha256::digest(text));
  if (digest != expected) {
    diagnostics.error(
        "FSIM-FE-VHSTD-002",
        "bundled IEEE 1076-2019 source failed its pinned checksum: "
            + path.generic_string());
    return std::nullopt;
  }
  return text;
}

CheckedSource checked_source(
    const std::filesystem::path& path,
    const std::string_view text) {
  CheckedSource source;
  source.path = path;
  source.content_digest = support::Sha256::hex(
      support::Sha256::digest(text));
  compiler::CacheKeyBuilder key;
  key.add(
      "compilation-unit-snapshot-schema",
      "fsim-hdl-compilation-unit-v1");
  key.add("input-path", path.lexically_normal().generic_string());
  key.add("input-content", source.content_digest);
  source.compilation_unit_digest = key.finish();
  return source;
}

std::optional<std::vector<frontend::DesignUnit>> projected_units(
    const std::string_view package,
    const std::filesystem::path& declaration_path,
    const std::filesystem::path& body_path,
    diagnostic::Engine& diagnostics) {
  std::vector<frontend::DesignUnit> units;
  std::string dependency;
  if (package == "numeric_std" || package == "std_logic_textio") {
    dependency = "library ieee;\nuse ieee.std_logic_1164.all;\n";
  } else if (package == "fixed_generic_pkg") {
    dependency =
        "library ieee;\nuse ieee.std_logic_1164.all;\n"
        "use ieee.numeric_std.all;\nuse ieee.math_real.all;\n"
        "use ieee.fixed_float_types.all;\n";
  } else if (package == "fixed_pkg") {
    dependency = "library ieee;\nuse ieee.fixed_generic_pkg.all;\n";
  } else if (package == "float_generic_pkg") {
    dependency =
        "library ieee;\nuse ieee.std_logic_1164.all;\n"
        "use ieee.numeric_std.all;\nuse ieee.math_real.all;\n"
        "use ieee.fixed_float_types.all;\nuse ieee.fixed_pkg.all;\n";
  } else if (package == "float_pkg") {
    dependency = "library ieee;\nuse ieee.float_generic_pkg.all;\n";
  }
  const auto declaration_projection = dependency
      + "package " + std::string{package} + " is\n"
      + "end package " + std::string{package} + ";\n";
  const auto body_projection =
      "package body " + std::string{package} + " is\n"
      + "end package body " + std::string{package} + ";\n";
  std::vector<std::pair<std::filesystem::path, std::string_view>> sources;
  sources.emplace_back(declaration_path, declaration_projection);
  if (!body_path.empty()) {
    sources.emplace_back(body_path, body_projection);
  }
  for (const auto& [path, projection] : sources) {
    auto parsed = frontend::parse(
        frontend::SourceText{path.generic_string(), std::string{projection}},
        frontend::Language::Vhdl2008);
    if (!parsed.diagnostics.empty() || parsed.design.units.size() != 1) {
      diagnostics.error(
          "FSIM-FE-VHSTD-003",
          "internal ieee." + std::string{package}
              + " intrinsic projection is invalid",
          {path.generic_string(), {}, {}});
      return std::nullopt;
    }
    auto unit = std::move(parsed.design.units.front());
    unit.library = "ieee";
    unit.standard_package_revision =
        "ieee-p1076:1076-2019:16a012320947d378611cc7457f64ed76cb52bac4";
    if (unit.primary_name.empty() && package == "std_logic_1164") {
      unit.standard_package_declarations = {
          "std_ulogic", "std_ulogic_vector", "std_logic",
          "std_logic_vector", "x01", "x01z", "ux01", "ux01z",
          "resolved", "and", "nand", "or", "nor", "xor", "xnor",
          "not", "sll", "srl", "rol", "ror", "to_bit",
          "to_bitvector", "to_stdulogic", "to_stdlogicvector",
          "to_stdulogicvector", "to_01", "to_x01", "to_x01z",
          "to_ux01", "??", "rising_edge", "falling_edge", "is_x",
          "to_string", "to_ostring", "to_hstring", "read", "write",
          "oread", "hread", "owrite", "hwrite",
      };
    } else if (unit.primary_name.empty()
               && package == "std_logic_textio") {
      unit.standard_package_declarations = {
          "read", "write", "hread", "hwrite", "oread", "owrite",
      };
    } else if (unit.primary_name.empty()
               && (package == "fixed_generic_pkg"
                   || package == "fixed_pkg")) {
      unit.standard_package_declarations = {
          "ufixed", "sfixed", "fixed_round_style", "fixed_overflow_style",
          "fixed_round", "fixed_truncate", "fixed_saturate", "fixed_wrap",
          "to_ufixed", "to_sfixed", "resize", "+", "-", "*", "/",
          "rem", "mod", "=", "/=", "<", "<=", ">", ">=",
      };
    } else if (unit.primary_name.empty()
               && (package == "float_generic_pkg"
                   || package == "float_pkg")) {
      unit.standard_package_declarations = {
          "float", "unresolved_float", "float32", "valid_fpstate",
          "float_exponent_width", "float_fraction_width",
          "float_round_style", "float_denormalize", "float_check_error",
          "to_float", "to_slv", "to_integer", "classfp", "finite",
          "isnan", "unordered", "is_negative", "zerofp", "neg_zerofp",
          "nanfp", "qnanfp", "pos_inffp", "neg_inffp", "add",
          "subtract", "multiply", "divide", "sqrt", "eq", "ne",
          "lt", "le", "gt", "ge",
      };
    } else if (unit.primary_name.empty()) {
      unit.standard_package_declarations = {
          "signed", "unsigned", "abs", "+", "-", "*", "/", "mod",
          "rem", "**", "=", "/=", "<", "<=", ">", ">=", "sll",
          "srl", "rol", "ror", "shift_left", "shift_right",
          "rotate_left", "rotate_right", "resize", "to_integer",
          "to_unsigned", "to_signed", "find_leftmost", "find_rightmost",
          "minimum", "maximum",
      };
    }
    units.push_back(std::move(unit));
  }
  return units;
}

}  // namespace

void inject_vhdl_standard_libraries(
    CheckedProject& checked,
    diagnostic::Engine& diagnostics) {
  const bool numeric_bit = uses_package(checked.parsed, "numeric_bit");
  const bool float_pkg = uses_package(checked.parsed, "float_pkg");
  const bool float_generic = float_pkg
      || uses_package(checked.parsed, "float_generic_pkg");
  const bool fixed_pkg = uses_package(checked.parsed, "fixed_pkg");
  const bool fixed_generic = float_generic || fixed_pkg
      || uses_package(checked.parsed, "fixed_generic_pkg");
  const bool fixed_types = fixed_generic
      || uses_package(checked.parsed, "fixed_float_types");
  const bool math_real = fixed_generic
      || uses_package(checked.parsed, "math_real");
  const bool numeric_std = fixed_generic
      || uses_package(checked.parsed, "numeric_std");
  const bool logic_textio =
      uses_package(checked.parsed, "std_logic_textio");
  const bool std_logic = numeric_std || logic_textio
      || uses_package(checked.parsed, "std_logic_1164");
  if (!std_logic && !logic_textio && !numeric_bit && !numeric_std
      && !math_real && !fixed_types && !fixed_generic && !fixed_pkg
      && !float_generic && !float_pkg) {
    return;
  }
  const auto root = library_root();
  std::vector<CheckedSource> sources;
  std::vector<frontend::DesignUnit> units;
  for (const auto& package : kPackages) {
    const bool requested = package.name == "std_logic_1164" ? std_logic
        : package.name == "std_logic_textio" ? logic_textio
        : package.name == "numeric_bit" ? numeric_bit
        : package.name == "numeric_std" ? numeric_std
        : package.name == "math_real" ? math_real
        : package.name == "fixed_float_types" ? fixed_types
        : package.name == "fixed_generic_pkg" ? fixed_generic
        : package.name == "fixed_pkg" ? fixed_pkg || float_generic
        : package.name == "float_generic_pkg" ? float_generic
                                                : float_pkg;
    if (!requested) {
      continue;
    }
    const bool conflict = std::ranges::any_of(
        checked.parsed.units,
        [&](const frontend::DesignUnit& unit) {
          return unit.language == frontend::Language::Vhdl2008
              && unit.kind == frontend::UnitKind::VhdlPackage
              && unit.library == "ieee"
              && unit.name == package.name;
        });
    if (conflict) {
      diagnostics.error(
          "FSIM-FE-VHSTD-004",
          "the compiler-supplied ieee." + std::string{package.name}
              + " package cannot be redeclared by a project source");
      return;
    }
    const auto declaration_path =
        root / std::filesystem::path{package.declaration};
    const auto body_path = package.body.empty()
        ? std::filesystem::path{}
        : root / std::filesystem::path{package.body};
    const auto declaration = checked_source_text(
        declaration_path, package.declaration_hash, diagnostics);
    const auto body = package.body.empty()
        ? std::optional<std::string>{std::string{}}
        : checked_source_text(
              body_path, package.body_hash, diagnostics);
    if (!declaration || !body) {
      return;
    }
    auto package_units = projected_units(
        package.name, declaration_path, body_path, diagnostics);
    if (!package_units) {
      return;
    }
    sources.push_back(checked_source(declaration_path, *declaration));
    if (!package.body.empty()) {
      sources.push_back(checked_source(body_path, *body));
    }
    units.insert(
        units.end(),
        std::make_move_iterator(package_units->begin()),
        std::make_move_iterator(package_units->end()));
  }
  checked.standard_sources.insert(
      checked.standard_sources.end(),
      std::make_move_iterator(sources.begin()),
      std::make_move_iterator(sources.end()));
  checked.parsed.units.insert(
      checked.parsed.units.begin(),
      std::make_move_iterator(units.begin()),
      std::make_move_iterator(units.end()));
}

}  // namespace fsim::app::application_detail
