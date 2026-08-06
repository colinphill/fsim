// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "fsim/support/path.hpp"

namespace fsim::app::application_detail {
namespace {

struct PackageSource {
  std::string_view name;
  std::string_view declaration;
  std::string_view declaration_hash;
  std::string_view body;
  std::string_view body_hash;
};

constexpr std::string_view kIeeePackageRevision =
    "ieee-p1076:1076-2019:16a012320947d378611cc7457f64ed76cb52bac4";
constexpr std::string_view kVitalPackageRevision =
    "ieee-vital:2000:fsim-clean-room-v1";

constexpr std::string_view kVitalTimingSource = R"vhdl(-- SPDX-License-Identifier: Apache-2.0
library ieee;
use ieee.std_logic_1164.all;
package vital_timing is
  -- The public clean-room interface is represented by fsim's typed intrinsic
  -- package metadata. No third-party VITAL package body is redistributed.
end package vital_timing;
)vhdl";

constexpr std::string_view kVitalPrimitivesSource = R"vhdl(-- SPDX-License-Identifier: Apache-2.0
library ieee;
use ieee.std_logic_1164.all;
use ieee.vital_timing.all;
package vital_primitives is
  -- The public clean-room interface is represented by fsim's typed intrinsic
  -- package metadata. No third-party VITAL package body is redistributed.
end package vital_primitives;
)vhdl";

constexpr std::string_view kVitalMemorySource = R"vhdl(-- SPDX-License-Identifier: Apache-2.0
library ieee;
use ieee.std_logic_1164.all;
use ieee.vital_timing.all;
use ieee.vital_primitives.all;
package vital_memory is
  -- The public clean-room interface is represented by fsim's typed intrinsic
  -- package metadata. No third-party VITAL package body is redistributed.
end package vital_memory;
)vhdl";

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
            + fsim::support::path_to_utf8(path));
    return std::nullopt;
  }
  std::string text{
      std::istreambuf_iterator<char>(stream),
      std::istreambuf_iterator<char>()};
  if (!stream.good() && !stream.eof()) {
    diagnostics.error(
        "FSIM-FE-VHSTD-001",
        "bundled IEEE 1076-2019 source could not be read: "
            + fsim::support::path_to_utf8(path));
    return std::nullopt;
  }
  const auto digest = support::Sha256::hex(
      support::Sha256::digest(text));
  if (digest != expected) {
    diagnostics.error(
        "FSIM-FE-VHSTD-002",
        "bundled IEEE 1076-2019 source failed its pinned checksum: "
            + fsim::support::path_to_utf8(path));
    return std::nullopt;
  }
  return text;
}

CheckedSource checked_source(
    const std::filesystem::path& path,
    const std::string_view text,
    std::filesystem::path backing_path = {}) {
  CheckedSource source;
  source.path = path;
  source.backing_path = std::move(backing_path);
  source.content_digest = support::Sha256::hex(
      support::Sha256::digest(text));
  compiler::CacheKeyBuilder key;
  key.add(
      "compilation-unit-snapshot-schema",
      "fsim-hdl-compilation-unit-v1");
  key.add(
      "input-path",
      fsim::support::path_to_utf8(path.lexically_normal()));
  key.add("input-content", source.content_digest);
  source.compilation_unit_digest = key.finish();
  return source;
}

std::optional<std::vector<frontend::DesignUnit>> projected_units(
    const std::string_view package,
    const std::filesystem::path& declaration_path,
    const std::filesystem::path& body_path,
    diagnostic::Engine& diagnostics,
    const std::string_view revision = kIeeePackageRevision) {
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
  } else if (package == "vital_timing") {
    dependency = "library ieee;\nuse ieee.std_logic_1164.all;\n";
  } else if (package == "vital_primitives") {
    dependency =
        "library ieee;\nuse ieee.std_logic_1164.all;\n"
        "use ieee.vital_timing.all;\n";
  } else if (package == "vital_memory") {
    dependency =
        "library ieee;\nuse ieee.std_logic_1164.all;\n"
        "use ieee.vital_timing.all;\n"
        "use ieee.vital_primitives.all;\n";
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
        frontend::SourceText{
            fsim::support::path_to_utf8(path), std::string{projection}},
        frontend::Language::Vhdl2008);
    if (!parsed.diagnostics.empty() || parsed.design.units.size() != 1) {
      diagnostics.error(
          "FSIM-FE-VHSTD-003",
          "internal ieee." + std::string{package}
              + " intrinsic projection is invalid",
          {fsim::support::path_to_utf8(path), {}, {}});
      return std::nullopt;
    }
    auto unit = std::move(parsed.design.units.front());
    unit.library = "ieee";
    unit.standard_package_revision = std::string{revision};
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
    } else if (unit.primary_name.empty() && package == "vital_timing") {
      unit.standard_package_declarations = {
          "vitaltransitiontype", "vitaldelaytype", "vitaldelaytype01",
          "vitaldelaytype01z", "vitaldelaytype01zx",
          "vitaldelayarraytype", "vitaldelayarraytype01",
          "vitaldelayarraytype01z", "vitaldelayarraytype01zx",
          "vitalzerodelay", "vitalzerodelay01", "vitalzerodelay01z",
          "vitalzerodelay01zx", "vitaloutputmaptype",
          "vitalresultmaptype", "vitalresultzmaptype",
          "vitaldefaultoutputmap", "vitaldefaultresultmap",
          "vitaldefaultresultzmap", "vitaltablesymboltype",
          "vitaledgesymboltype", "vitaltimearrayt", "vitaltimearraypt",
          "vitalboolarrayt", "vitalboolarraypt", "vitallogicarraypt",
          "vitaltimingdatatype", "vitaltimingdatainit",
          "vitalperioddatatype", "vitalperioddatainit",
          "vitalglitchkindtype", "vitalglitchdatatype",
          "vitalglitchdataarraytype", "vitalskewexpectedtype",
          "vitalskewdatatype", "vitalskewdatainit",
          "vitalpathtype", "vitalpath01type", "vitalpath01ztype",
          "vitalpatharraytype", "vitalpatharray01type",
          "vitalpatharray01ztype", "vitalpathdelay",
          "vitalpathdelay01", "vitalpathdelay01z", "vitalwiredelay",
          "vitalsignaldelay",
          "vitalextendtofilldelay", "vitalcalcdelay",
          "vitalsetupholdcheck", "vitalrecoveryremovalcheck",
          "vitalperiodpulsecheck", "vitalinphaseskewcheck",
          "vitaloutphaseskewcheck",
      };
    } else if (unit.primary_name.empty()
               && package == "vital_primitives") {
      unit.standard_package_declarations = {
          "vitaltruthsymboltype", "vitalstatesymboltype",
          "vitaltruthtabletype", "vitalstatetabletype",
          "vitaldefdelay01", "vitaldefdelay01z", "vitaland",
          "vitalor", "vitalxor", "vitalnand", "vitalnor",
          "vitalxnor", "vitaland2", "vitalor2", "vitalxor2",
          "vitalnand2", "vitalnor2", "vitalxnor2", "vitaland3",
          "vitalor3", "vitalxor3", "vitalnand3", "vitalnor3",
          "vitalxnor3", "vitaland4", "vitalor4", "vitalxor4",
          "vitalnand4", "vitalnor4", "vitalxnor4", "vitalbuf",
          "vitalbufif0", "vitalbufif1", "vitalident", "vitalinv",
          "vitalinvif0", "vitalinvif1", "vitalmux", "vitalmux2",
          "vitalmux4", "vitalmux8", "vitaldecoder",
          "vitaldecoder2", "vitaldecoder4", "vitaldecoder8",
          "vitaltruthtable", "vitalstatetable",
      };
    } else if (unit.primary_name.empty() && package == "vital_memory") {
      unit.standard_package_declarations = {
          "vitalmemoryarctype", "outputretainbehaviortype",
          "vitalmemorymsgformattype", "x01arrayt", "x01arraypt",
          "vitalmemoryviolationtype", "defaultnumbitspersubword",
          "vitalmemoryscheduledatatype", "vitalmemorytimingdatatype",
          "vitalperioddataarraytype", "vitalmemoryscheduledatavectortype",
          "vitalportstatetype", "vitalportflagtype",
          "vitaldefaultportflag", "vitalportflagvectortype",
          "memorywordtype", "memorywordptr", "memoryarraytype",
          "memoryarrayptrtype", "vitalmemoryarrayrectype",
          "vitalmemorydatatype", "vitaltimingdatavectortype",
          "vitalmemoryviolflagsizetype", "vitalmemorysymboltype",
          "vitalmemorytabletype", "vitalmemoryviolationsymboltype",
          "vitalmemoryviolationtabletype", "vitalporttype",
          "vitalcrossportmodetype", "vitaladdressvaluetype",
          "vitaladdressvaluevectortype", "vitalmemoryinitpathdelay",
          "vitalmemoryaddpathdelay", "vitalmemoryschedulepathdelay",
          "vitalmemorytimingdatainit", "vitalmemorysetupholdcheck",
          "vitalmemoryperiodpulsecheck", "vitaldeclarememory",
          "vitalmemorytable", "vitalmemorycrossports",
          "vitalmemoryviolation",
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

frontend::Type vital_scalar_type(
    const std::string_view spelling,
    const frontend::ValueDomain domain,
    const frontend::SourceSpan& span) {
  frontend::Type type;
  type.spelling = std::string{spelling};
  type.domain = domain;
  type.named_type_span = span;
  if (domain == frontend::ValueDomain::Integer) {
    type.is_signed = true;
    type.packed_range = frontend::PackedRange{63, 0, true};
    type.integer_range = frontend::IntegerRange{
        0, std::numeric_limits<std::int64_t>::max(), false};
    type.nominal_type = "@builtin:time";
    type.vhdl_type_declaration = type.nominal_type;
  }
  return type;
}

frontend::Type vital_enumeration_type(
    const std::string_view name,
    std::vector<std::string> literals,
    const frontend::SourceSpan& span) {
  frontend::Type type;
  type.spelling = std::string{name};
  type.domain = frontend::ValueDomain::Bit2;
  type.nominal_type = "@fsim-vital:" + std::string{name};
  type.vhdl_type_declaration = type.nominal_type;
  type.named_type_span = span;
  type.enumeration_literals = std::move(literals);
  std::uint64_t width = 1;
  auto maximum = type.enumeration_literals.empty()
      ? std::uint64_t{0}
      : static_cast<std::uint64_t>(type.enumeration_literals.size() - 1U);
  while (maximum > 1U) {
    ++width;
    maximum >>= 1U;
  }
  type.packed_range = frontend::PackedRange{
      static_cast<std::int64_t>(width - 1U), 0, true};
  type.enumeration_range = frontend::EnumerationRange{
      0,
      static_cast<std::int64_t>(type.enumeration_literals.size() - 1U),
      false};
  return type;
}

frontend::Type vital_integer_type(
    const std::string_view spelling,
    const frontend::SourceSpan& span,
    const std::int64_t minimum = std::numeric_limits<std::int32_t>::min(),
    const std::int64_t maximum = std::numeric_limits<std::int32_t>::max()) {
  frontend::Type type;
  type.spelling = std::string{spelling};
  type.domain = frontend::ValueDomain::Integer;
  type.is_signed = true;
  type.packed_range = frontend::PackedRange{31, 0, true};
  type.integer_range = frontend::IntegerRange{minimum, maximum, false};
  type.nominal_type = "@builtin:integer";
  type.vhdl_type_declaration = type.nominal_type;
  type.named_type_span = span;
  return type;
}

frontend::Type vital_array_type(
    const std::string_view name,
    const std::string_view index_subtype,
    const std::optional<std::size_t> element_count,
    frontend::Type element,
    const frontend::SourceSpan& span,
    const std::optional<frontend::IntegerRange> index_base = std::nullopt) {
  frontend::Type type;
  type.spelling = std::string{name};
  type.domain = element.domain;
  type.nominal_type = "@fsim-vital:" + std::string{name};
  type.vhdl_type_declaration = type.nominal_type;
  type.named_type_span = span;
  frontend::VhdlArrayDimension dimension;
  dimension.index_subtype = std::string{index_subtype};
  dimension.index_span = span;
  dimension.index_base_range = index_base;
  dimension.unconstrained = !element_count.has_value();
  frontend::VhdlArrayInfo array;
  array.index_subtype = dimension.index_subtype;
  array.index_span = span;
  array.index_base_range = index_base;
  array.element_spelling = element.spelling;
  array.element_span = span;
  array.element_domain = element.domain;
  array.unconstrained = dimension.unconstrained;
  const auto element_width = element.width();
  if (element_count) {
    const auto last = static_cast<std::int64_t>(*element_count - 1U);
    dimension.range = frontend::IntegerRange{0, last, false};
    dimension.stride = element_width.value_or(0);
    array.flat_width = element_width
        ? std::optional<std::uint64_t>{
              *element_width * static_cast<std::uint64_t>(*element_count)}
        : std::nullopt;
    type.packed_range = frontend::PackedRange{0, last, false};
  }
  array.dimensions.push_back(std::move(dimension));
  array.element_types.push_back(std::move(element));
  type.vhdl_array = std::move(array);
  return type;
}

frontend::Type vital_two_dimensional_table_type(
    const std::string_view name,
    frontend::Type element,
    const frontend::SourceSpan& span) {
  auto type = vital_array_type(
      name, "natural", std::nullopt, std::move(element), span,
      frontend::IntegerRange{
          0, std::numeric_limits<std::int32_t>::max(), false});
  auto& array = *type.vhdl_array;
  array.dimensions.push_back(array.dimensions.front());
  return type;
}

frontend::Type vital_access_type(
    const std::string_view name,
    frontend::Type designated,
    const frontend::SourceSpan& span) {
  frontend::Type type;
  type.spelling = std::string{name};
  type.domain = frontend::ValueDomain::Bit2;
  type.nominal_type = "@fsim-vital:" + std::string{name};
  type.vhdl_type_declaration = type.nominal_type;
  type.named_type_span = span;
  frontend::VhdlAccessInfo access;
  access.designated_span = span;
  access.designated_types.push_back(std::move(designated));
  type.packed_range = frontend::PackedRange{
      static_cast<std::int64_t>(access.handle_width - 1U), 0, true};
  type.vhdl_access = std::move(access);
  return type;
}

frontend::Type vital_record_type(
    const std::string_view name,
    std::vector<std::pair<std::string, frontend::Type>> fields,
    const frontend::SourceSpan& span) {
  frontend::Type type;
  type.spelling = std::string{name};
  type.domain = frontend::ValueDomain::Bit2;
  type.nominal_type = "@fsim-vital:" + std::string{name};
  type.vhdl_type_declaration = type.nominal_type;
  type.named_type_span = span;
  type.packed_aggregate = frontend::PackedAggregateKind::Struct;
  std::uint64_t width = 0;
  for (auto& [field_name, field_type] : fields) {
    const auto field_width = field_type.width();
    if (!field_width || *field_width == 0
        || *field_width > std::numeric_limits<std::uint64_t>::max() - width) {
      type.packed_range.reset();
      width = 0;
      break;
    }
    if (field_type.domain == frontend::ValueDomain::Logic9) {
      type.domain = frontend::ValueDomain::Logic9;
    }
    type.packed_members.push_back(frontend::PackedMember{
        std::move(field_name), field_type.domain, field_type.spelling,
        field_type.packed_range, field_type.is_signed,
        field_type.packed_range_expression, 0, span,
        std::vector<frontend::Type>{std::move(field_type)},
        std::nullopt});
    width += *field_width;
  }
  if (width != 0) {
    auto offset = width;
    for (auto& member : type.packed_members) {
      offset -= *member.width();
      member.lsb_offset = offset;
    }
    type.packed_range = frontend::PackedRange{
        static_cast<std::int64_t>(width - 1U), 0, true};
  }
  return type;
}

void add_vital_alias(
    frontend::DesignUnit& unit,
    const std::string_view name,
    frontend::Type type,
    const frontend::TypeDeclarationKind kind) {
  std::vector<frontend::EnumLiteralDeclaration> literals;
  if (!type.enumeration_literals.empty()) {
    literals.reserve(type.enumeration_literals.size());
    for (std::size_t index = 0;
         index < type.enumeration_literals.size(); ++index) {
      literals.push_back(frontend::EnumLiteralDeclaration{
          type.enumeration_literals[index],
          frontend::Expression{
              frontend::ExpressionKind::IntegerLiteral,
              std::to_string(index), {}, unit.span},
          unit.span});
    }
  }
  unit.type_aliases.push_back(frontend::TypeAliasDeclaration{
      std::string{name}, std::move(type), unit.span, std::move(literals), kind});
}

void materialize_vital_types(frontend::DesignUnit& unit) {
  if (!unit.primary_name.empty()) {
    return;
  }
  const auto logic = vital_scalar_type(
      "std_ulogic", frontend::ValueDomain::Logic9, unit.span);
  const auto time = vital_scalar_type(
      "vitaldelaytype", frontend::ValueDomain::Integer, unit.span);
  const auto natural_base = frontend::IntegerRange{
      0, std::numeric_limits<std::int32_t>::max(), false};
  if (unit.name == "vital_timing") {
    auto transition = vital_enumeration_type(
        "vitaltransitiontype",
        {"tr01", "tr10", "tr0z", "trz1", "tr1z", "trz0",
         "tr0x", "trx1", "tr1x", "trx0", "trxz", "trzx"},
        unit.span);
    add_vital_alias(
        unit, "vitaltransitiontype", transition,
        frontend::TypeDeclarationKind::VhdlEnumeration);
    add_vital_alias(
        unit, "vitaldelaytype", time,
        frontend::TypeDeclarationKind::VhdlSubtype);
    const auto delay01 = vital_array_type(
        "vitaldelaytype01", "vitaltransitiontype", 2, time, unit.span,
        frontend::IntegerRange{0, 11, false});
    const auto delay01z = vital_array_type(
        "vitaldelaytype01z", "vitaltransitiontype", 6, time, unit.span,
        frontend::IntegerRange{0, 11, false});
    const auto delay01zx = vital_array_type(
        "vitaldelaytype01zx", "vitaltransitiontype", 12, time, unit.span,
        frontend::IntegerRange{0, 11, false});
    add_vital_alias(
        unit, "vitaldelaytype01", delay01,
        frontend::TypeDeclarationKind::VhdlArray);
    add_vital_alias(
        unit, "vitaldelaytype01z", delay01z,
        frontend::TypeDeclarationKind::VhdlArray);
    add_vital_alias(
        unit, "vitaldelaytype01zx", delay01zx,
        frontend::TypeDeclarationKind::VhdlArray);
    for (auto [name, element] : {
             std::pair{"vitaldelayarraytype", time},
             std::pair{"vitaldelayarraytype01", delay01},
             std::pair{"vitaldelayarraytype01z", delay01z},
             std::pair{"vitaldelayarraytype01zx", delay01zx}}) {
      add_vital_alias(
          unit, name,
          vital_array_type(
              name, "natural", std::nullopt, std::move(element),
              unit.span, natural_base),
          frontend::TypeDeclarationKind::VhdlArray);
    }
    for (const auto& [name, count] : {
             std::pair{"std_logic_vector2", std::size_t{2}},
             std::pair{"std_logic_vector3", std::size_t{3}},
             std::pair{"std_logic_vector4", std::size_t{4}},
             std::pair{"std_logic_vector8", std::size_t{8}}}) {
      auto vector = vital_array_type(
          name, "natural", count, logic, unit.span, natural_base);
      vector.nominal_type.clear();
      vector.vhdl_type_declaration = "@builtin:std_logic_vector";
      vector.packed_range = frontend::PackedRange{
          static_cast<std::int64_t>(count - 1U), 0, true};
      vector.vhdl_array->dimensions.front().range = frontend::IntegerRange{
          static_cast<std::int64_t>(count - 1U), 0, true};
      add_vital_alias(
          unit, name, std::move(vector),
          frontend::TypeDeclarationKind::VhdlSubtype);
    }
    for (const auto& [name, count] : {
             std::pair{"vitaloutputmaptype", std::size_t{9}},
             std::pair{"vitalresultmaptype", std::size_t{4}},
             std::pair{"vitalresultzmaptype", std::size_t{5}}}) {
      add_vital_alias(
          unit, name,
          vital_array_type(
              name, "std_ulogic", count, logic, unit.span,
              frontend::IntegerRange{0, 8, false}),
          frontend::TypeDeclarationKind::VhdlArray);
    }
    add_vital_alias(
        unit, "vitaltablesymboltype",
        vital_enumeration_type(
            "vitaltablesymboltype",
            {"'/'", "'\\'", "'P'", "'N'", "'r'", "'f'", "'p'",
             "'n'", "'R'", "'F'", "'^'", "'v'", "'E'", "'A'",
             "'D'", "'*'", "'X'", "'0'", "'1'", "'-'", "'B'",
             "'Z'", "'S'"},
            unit.span),
        frontend::TypeDeclarationKind::VhdlEnumeration);
    auto edge = unit.type_aliases.back().type;
    edge.spelling = "vitaledgesymboltype";
    edge.enumeration_range = frontend::EnumerationRange{0, 15, false};
    add_vital_alias(
        unit, "vitaledgesymboltype", std::move(edge),
        frontend::TypeDeclarationKind::VhdlSubtype);
    const auto boolean = vital_scalar_type(
        "boolean", frontend::ValueDomain::Boolean, unit.span);
    const auto time_array = vital_array_type(
        "vitaltimearrayt", "integer", std::nullopt, time, unit.span);
    const auto bool_array = vital_array_type(
        "vitalboolarrayt", "integer", std::nullopt, boolean, unit.span);
    add_vital_alias(
        unit, "vitaltimearrayt", time_array,
        frontend::TypeDeclarationKind::VhdlArray);
    add_vital_alias(
        unit, "vitalboolarrayt", bool_array,
        frontend::TypeDeclarationKind::VhdlArray);
    const auto time_access = vital_access_type(
        "vitaltimearraypt", time_array, unit.span);
    const auto bool_access = vital_access_type(
        "vitalboolarraypt", bool_array, unit.span);
    const auto logic_vector = vital_array_type(
        "std_logic_vector", "natural", std::nullopt, logic, unit.span,
        natural_base);
    const auto logic_access = vital_access_type(
        "vitallogicarraypt", logic_vector, unit.span);
    add_vital_alias(
        unit, "vitaltimearraypt", time_access,
        frontend::TypeDeclarationKind::VhdlAccess);
    add_vital_alias(
        unit, "vitalboolarraypt", bool_access,
        frontend::TypeDeclarationKind::VhdlAccess);
    add_vital_alias(
        unit, "vitallogicarraypt", logic_access,
        frontend::TypeDeclarationKind::VhdlAccess);
    add_vital_alias(
        unit, "vitaltimingdatatype",
        vital_record_type(
            "vitaltimingdatatype",
            {{"notfirstflag", boolean}, {"reflast", logic},
             {"reftime", time}, {"holden", boolean},
             {"testlast", logic}, {"testtime", time},
             {"setupen", boolean}, {"testlasta", logic_access},
             {"testtimea", time_access}, {"holdena", bool_access},
             {"setupena", bool_access}},
            unit.span),
        frontend::TypeDeclarationKind::VhdlRecord);
    add_vital_alias(
        unit, "vitalperioddatatype",
        vital_record_type(
            "vitalperioddatatype",
            {{"last", logic}, {"rise", time}, {"fall", time},
             {"notfirstflag", boolean}},
            unit.span),
        frontend::TypeDeclarationKind::VhdlRecord);
    auto glitch_kind = vital_enumeration_type(
        "vitalglitchkindtype",
        {"onevent", "ondetect", "vitalinertial", "vitaltransport"},
        unit.span);
    add_vital_alias(
        unit, "vitalglitchkindtype", glitch_kind,
        frontend::TypeDeclarationKind::VhdlEnumeration);
    const auto glitch_data = vital_record_type(
        "vitalglitchdatatype",
        {{"schedtime", time}, {"glitchtime", time},
         {"schedvalue", logic}, {"lastvalue", logic}},
        unit.span);
    add_vital_alias(
        unit, "vitalglitchdatatype", glitch_data,
        frontend::TypeDeclarationKind::VhdlRecord);
    add_vital_alias(
        unit, "vitalglitchdataarraytype",
        vital_array_type(
            "vitalglitchdataarraytype", "natural", std::nullopt,
            glitch_data, unit.span, natural_base),
        frontend::TypeDeclarationKind::VhdlArray);
    for (const auto& [record_name, array_name, delay_type] : {
             std::tuple{
                 "vitalpathtype", "vitalpatharraytype", time},
             std::tuple{
                 "vitalpath01type", "vitalpatharray01type", delay01},
             std::tuple{
                 "vitalpath01ztype", "vitalpatharray01ztype", delay01z}}) {
      auto record = vital_record_type(
          record_name,
          {{"inputchangetime", time}, {"pathdelay", delay_type},
           {"pathcondition", boolean}},
          unit.span);
      add_vital_alias(
          unit, record_name, record,
          frontend::TypeDeclarationKind::VhdlRecord);
      add_vital_alias(
          unit, array_name,
          vital_array_type(
              array_name, "natural", std::nullopt, std::move(record),
              unit.span, natural_base),
          frontend::TypeDeclarationKind::VhdlArray);
    }
    auto skew_expected = vital_enumeration_type(
        "vitalskewexpectedtype", {"none", "s1r", "s1f", "s2r", "s2f"},
        unit.span);
    add_vital_alias(
        unit, "vitalskewexpectedtype", skew_expected,
        frontend::TypeDeclarationKind::VhdlEnumeration);
    add_vital_alias(
        unit, "vitalskewdatatype",
        vital_record_type(
            "vitalskewdatatype",
            {{"expectedtype", skew_expected}, {"signal1old1", time},
             {"signal2old1", time}, {"signal1old2", time},
             {"signal2old2", time}},
            unit.span),
        frontend::TypeDeclarationKind::VhdlRecord);
    return;
  }
  if (unit.name == "vital_memory") {
    const auto boolean = vital_scalar_type(
        "boolean", frontend::ValueDomain::Boolean, unit.span);
    const auto integer = vital_integer_type("integer", unit.span);
    const auto positive = vital_integer_type(
        "positive", unit.span, 1, std::numeric_limits<std::int32_t>::max());
    const auto logic_vector = vital_array_type(
        "std_logic_vector", "natural", std::nullopt, logic, unit.span,
        natural_base);
    const auto x01_array = vital_array_type(
        "x01arrayt", "natural", std::nullopt, logic, unit.span,
        natural_base);
    const auto x01_access = vital_access_type(
        "x01arraypt", x01_array, unit.span);
    add_vital_alias(
        unit, "vitalmemoryarctype",
        vital_enumeration_type(
            "vitalmemoryarctype",
            {"parallelarc", "crossarc", "subwordarc"}, unit.span),
        frontend::TypeDeclarationKind::VhdlEnumeration);
    add_vital_alias(
        unit, "outputretainbehaviortype",
        vital_enumeration_type(
            "outputretainbehaviortype", {"bitcorrupt", "wordcorrupt"},
            unit.span),
        frontend::TypeDeclarationKind::VhdlEnumeration);
    add_vital_alias(
        unit, "vitalmemorymsgformattype",
        vital_enumeration_type(
            "vitalmemorymsgformattype", {"vector", "scalar", "vectorenum"},
            unit.span),
        frontend::TypeDeclarationKind::VhdlEnumeration);
    add_vital_alias(
        unit, "x01arrayt", x01_array,
        frontend::TypeDeclarationKind::VhdlArray);
    add_vital_alias(
        unit, "x01arraypt", x01_access,
        frontend::TypeDeclarationKind::VhdlAccess);
    auto violation_access = x01_access;
    violation_access.spelling = "vitalmemoryviolationtype";
    violation_access.nominal_type = "@fsim-vital:vitalmemoryviolationtype";
    violation_access.vhdl_type_declaration = violation_access.nominal_type;
    add_vital_alias(
        unit, "vitalmemoryviolationtype", std::move(violation_access),
        frontend::TypeDeclarationKind::VhdlAccess);
    const auto schedule_data = vital_record_type(
        "vitalmemoryscheduledatatype",
        {{"outputdata", logic}, {"numbitspersubword", integer},
         {"scheduletime", time}, {"schedulevalue", logic},
         {"lastoutputvalue", logic}, {"propdelay", time},
         {"outputretaindelay", time}, {"inputage", time}},
        unit.span);
    add_vital_alias(
        unit, "vitalmemoryscheduledatatype", schedule_data,
        frontend::TypeDeclarationKind::VhdlRecord);
    const auto time_array = vital_array_type(
        "vitaltimearrayt", "integer", std::nullopt, time, unit.span);
    const auto bool_array = vital_array_type(
        "vitalboolarrayt", "integer", std::nullopt, boolean, unit.span);
    const auto time_access = vital_access_type(
        "vitaltimearraypt", time_array, unit.span);
    const auto bool_access = vital_access_type(
        "vitalboolarraypt", bool_array, unit.span);
    const auto logic_access = vital_access_type(
        "vitallogicarraypt", logic_vector, unit.span);
    const auto memory_timing_data = vital_record_type(
        "vitalmemorytimingdatatype",
        {{"notfirstflag", boolean}, {"reflast", logic},
         {"reftime", time}, {"holden", boolean},
         {"testlast", logic}, {"testtime", time},
         {"setupen", boolean}, {"testlasta", logic_access},
         {"testtimea", time_access}, {"reflasta", x01_access},
         {"reftimea", time_access}, {"holdena", bool_access},
         {"setupena", bool_access}},
        unit.span);
    add_vital_alias(
        unit, "vitalmemorytimingdatatype", memory_timing_data,
        frontend::TypeDeclarationKind::VhdlRecord);
    const auto period_data = vital_record_type(
        "vitalperioddatatype",
        {{"last", logic}, {"rise", time}, {"fall", time},
         {"notfirstflag", boolean}}, unit.span);
    add_vital_alias(
        unit, "vitalperioddataarraytype",
        vital_array_type(
            "vitalperioddataarraytype", "natural", std::nullopt,
            period_data, unit.span, natural_base),
        frontend::TypeDeclarationKind::VhdlArray);
    add_vital_alias(
        unit, "vitalmemoryscheduledatavectortype",
        vital_array_type(
            "vitalmemoryscheduledatavectortype", "natural", std::nullopt,
            schedule_data, unit.span, natural_base),
        frontend::TypeDeclarationKind::VhdlArray);
    auto port_state = vital_enumeration_type(
        "vitalportstatetype",
        {"undef", "read", "write", "corrupt", "highz"}, unit.span);
    add_vital_alias(
        unit, "vitalportstatetype", port_state,
        frontend::TypeDeclarationKind::VhdlEnumeration);
    const auto port_flag = vital_record_type(
        "vitalportflagtype",
        {{"memorycurrent", port_state}, {"memoryprevious", port_state},
         {"datacurrent", port_state}, {"dataprevious", port_state},
         {"outputdisable", boolean}}, unit.span);
    add_vital_alias(
        unit, "vitalportflagtype", port_flag,
        frontend::TypeDeclarationKind::VhdlRecord);
    add_vital_alias(
        unit, "vitalportflagvectortype",
        vital_array_type(
            "vitalportflagvectortype", "natural", std::nullopt,
            port_flag, unit.span, natural_base),
        frontend::TypeDeclarationKind::VhdlArray);
    const auto memory_word = vital_array_type(
        "memorywordtype", "natural", std::nullopt, logic, unit.span,
        natural_base);
    const auto memory_word_ptr = vital_access_type(
        "memorywordptr", memory_word, unit.span);
    add_vital_alias(
        unit, "memorywordtype", memory_word,
        frontend::TypeDeclarationKind::VhdlArray);
    add_vital_alias(
        unit, "memorywordptr", memory_word_ptr,
        frontend::TypeDeclarationKind::VhdlAccess);
    const auto memory_array = vital_array_type(
        "memoryarraytype", "natural", std::nullopt, memory_word_ptr,
        unit.span, natural_base);
    const auto memory_array_ptr = vital_access_type(
        "memoryarrayptrtype", memory_array, unit.span);
    add_vital_alias(
        unit, "memoryarraytype", memory_array,
        frontend::TypeDeclarationKind::VhdlArray);
    add_vital_alias(
        unit, "memoryarrayptrtype", memory_array_ptr,
        frontend::TypeDeclarationKind::VhdlAccess);
    const auto memory_record = vital_record_type(
        "vitalmemoryarrayrectype",
        {{"noofwords", positive}, {"noofbitsperword", positive},
         {"noofbitspersubword", positive}, {"noofbitsperenable", positive},
         {"memoryarrayptr", memory_array_ptr}}, unit.span);
    add_vital_alias(
        unit, "vitalmemoryarrayrectype", memory_record,
        frontend::TypeDeclarationKind::VhdlRecord);
    add_vital_alias(
        unit, "vitalmemorydatatype",
        vital_access_type("vitalmemorydatatype", memory_record, unit.span),
        frontend::TypeDeclarationKind::VhdlAccess);
    const auto timing_data = vital_record_type(
        "vitaltimingdatatype",
        {{"notfirstflag", boolean}, {"reflast", logic},
         {"reftime", time}, {"holden", boolean},
         {"testlast", logic}, {"testtime", time},
         {"setupen", boolean}, {"testlasta", logic_access},
         {"testtimea", time_access}, {"holdena", bool_access},
         {"setupena", bool_access}}, unit.span);
    add_vital_alias(
        unit, "vitaltimingdatavectortype",
        vital_array_type(
            "vitaltimingdatavectortype", "natural", std::nullopt,
            timing_data, unit.span, natural_base),
        frontend::TypeDeclarationKind::VhdlArray);
    add_vital_alias(
        unit, "vitalmemoryviolflagsizetype",
        vital_array_type(
            "vitalmemoryviolflagsizetype", "natural", std::nullopt,
            integer, unit.span, natural_base),
        frontend::TypeDeclarationKind::VhdlArray);
    auto memory_symbol = vital_enumeration_type(
        "vitalmemorysymboltype",
        {"'/'", "'\\'", "'P'", "'N'", "'r'", "'f'", "'p'",
         "'n'", "'R'", "'F'", "'^'", "'v'", "'E'", "'A'", "'D'",
         "'*'", "'X'", "'0'", "'1'", "'-'", "'B'", "'Z'", "'S'",
         "'g'", "'u'", "'i'", "'G'", "'U'", "'I'", "'w'", "'s'",
         "'c'", "'l'", "'d'", "'e'", "'C'", "'L'", "'M'", "'m'",
         "'t'"}, unit.span);
    add_vital_alias(
        unit, "vitalmemorysymboltype", memory_symbol,
        frontend::TypeDeclarationKind::VhdlEnumeration);
    add_vital_alias(
        unit, "vitalmemorytabletype",
        vital_two_dimensional_table_type(
            "vitalmemorytabletype", memory_symbol, unit.span),
        frontend::TypeDeclarationKind::VhdlArray);
    auto violation_symbol = vital_enumeration_type(
        "vitalmemoryviolationsymboltype", {"'X'", "'0'", "'-'"},
        unit.span);
    add_vital_alias(
        unit, "vitalmemoryviolationsymboltype", violation_symbol,
        frontend::TypeDeclarationKind::VhdlEnumeration);
    add_vital_alias(
        unit, "vitalmemoryviolationtabletype",
        vital_two_dimensional_table_type(
            "vitalmemoryviolationtabletype", violation_symbol, unit.span),
        frontend::TypeDeclarationKind::VhdlArray);
    add_vital_alias(
        unit, "vitalporttype",
        vital_enumeration_type(
            "vitalporttype", {"undef", "read", "write", "rdnwr"},
            unit.span),
        frontend::TypeDeclarationKind::VhdlEnumeration);
    add_vital_alias(
        unit, "vitalcrossportmodetype",
        vital_enumeration_type(
            "vitalcrossportmodetype",
            {"cpread", "writecontention", "readwritecontention",
             "cpreadandwritecontention", "cpreadandreadcontention"},
            unit.span),
        frontend::TypeDeclarationKind::VhdlEnumeration);
    auto address = integer;
    address.spelling = "vitaladdressvaluetype";
    add_vital_alias(
        unit, "vitaladdressvaluetype", address,
        frontend::TypeDeclarationKind::VhdlSubtype);
    add_vital_alias(
        unit, "vitaladdressvaluevectortype",
        vital_array_type(
            "vitaladdressvaluevectortype", "natural", std::nullopt,
            address, unit.span, natural_base),
        frontend::TypeDeclarationKind::VhdlArray);
    return;
  }
  if (unit.name == "vital_primitives") {
    auto table = vital_enumeration_type(
        "vitaltablesymboltype",
        {"'/'", "'\\'", "'P'", "'N'", "'r'", "'f'", "'p'",
         "'n'", "'R'", "'F'", "'^'", "'v'", "'E'", "'A'", "'D'",
         "'*'", "'X'", "'0'", "'1'", "'-'", "'B'", "'Z'", "'S'"},
        unit.span);
    auto truth = table;
    truth.spelling = "vitaltruthsymboltype";
    truth.enumeration_range = frontend::EnumerationRange{16, 21, false};
    add_vital_alias(
        unit, "vitaltruthsymboltype", truth,
        frontend::TypeDeclarationKind::VhdlSubtype);
    auto state = table;
    state.spelling = "vitalstatesymboltype";
    state.enumeration_range = frontend::EnumerationRange{0, 22, false};
    add_vital_alias(
        unit, "vitalstatesymboltype", state,
        frontend::TypeDeclarationKind::VhdlSubtype);
    add_vital_alias(
        unit, "vitaltruthtabletype",
        vital_two_dimensional_table_type(
            "vitaltruthtabletype", truth, unit.span),
        frontend::TypeDeclarationKind::VhdlArray);
    add_vital_alias(
        unit, "vitalstatetabletype",
        vital_two_dimensional_table_type(
            "vitalstatetabletype", state, unit.span),
        frontend::TypeDeclarationKind::VhdlArray);
  }
}

}  // namespace

void inject_vhdl_standard_libraries(
    CheckedProject& checked,
    diagnostic::Engine& diagnostics) {
  const bool vital_memory = uses_package(checked.parsed, "vital_memory");
  const bool vital_primitives = vital_memory
      || uses_package(checked.parsed, "vital_primitives");
  const bool vital_timing = vital_primitives
      || uses_package(checked.parsed, "vital_timing");
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
  const bool std_logic = vital_timing || numeric_std || logic_textio
      || uses_package(checked.parsed, "std_logic_1164");
  if (!std_logic && !logic_textio && !numeric_bit && !numeric_std
      && !math_real && !fixed_types && !fixed_generic && !fixed_pkg
      && !float_generic && !float_pkg && !vital_timing
      && !vital_primitives && !vital_memory) {
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
    const auto declaration_backing_path =
        root / std::filesystem::path{package.declaration};
    const auto body_backing_path = package.body.empty()
        ? std::filesystem::path{}
        : root / std::filesystem::path{package.body};
    const auto declaration_path = std::filesystem::path{"fsim-standard"}
        / std::filesystem::path{package.declaration};
    const auto body_path = package.body.empty()
        ? std::filesystem::path{}
        : std::filesystem::path{"fsim-standard"}
            / std::filesystem::path{package.body};
    const auto declaration = checked_source_text(
        declaration_backing_path, package.declaration_hash, diagnostics);
    const auto body = package.body.empty()
        ? std::optional<std::string>{std::string{}}
        : checked_source_text(
              body_backing_path, package.body_hash, diagnostics);
    if (!declaration || !body) {
      return;
    }
    auto package_units = projected_units(
        package.name, declaration_path, body_path, diagnostics);
    if (!package_units) {
      return;
    }
    sources.push_back(checked_source(
        declaration_path, *declaration, declaration_backing_path));
    if (!package.body.empty()) {
      sources.push_back(checked_source(
          body_path, *body, body_backing_path));
    }
    units.insert(
        units.end(),
        std::make_move_iterator(package_units->begin()),
        std::make_move_iterator(package_units->end()));
  }
  const auto inject_vital = [&](const std::string_view package,
                                const std::string_view source_text) {
    const bool conflict = std::ranges::any_of(
        checked.parsed.units,
        [&](const frontend::DesignUnit& unit) {
          return unit.language == frontend::Language::Vhdl2008
              && unit.kind == frontend::UnitKind::VhdlPackage
              && unit.library == "ieee" && unit.name == package;
        });
    if (conflict) {
      diagnostics.error(
          "FSIM-FE-VHSTD-004",
          "the compiler-supplied ieee." + std::string{package}
              + " package cannot be redeclared by a project source");
      return false;
    }
    const auto path = std::filesystem::path{"fsim-standard"} / "ieee"
        / (std::string{package} + ".vhdl");
    auto package_units = projected_units(
        package, path, {}, diagnostics, kVitalPackageRevision);
    if (!package_units) {
      return false;
    }
    for (auto& unit : *package_units) {
      materialize_vital_types(unit);
    }
    sources.push_back(checked_source(path, source_text));
    units.insert(
        units.end(),
        std::make_move_iterator(package_units->begin()),
        std::make_move_iterator(package_units->end()));
    return true;
  };
  if (vital_timing
      && !inject_vital("vital_timing", kVitalTimingSource)) {
    return;
  }
  if (vital_primitives
      && !inject_vital("vital_primitives", kVitalPrimitivesSource)) {
    return;
  }
  if (vital_memory
      && !inject_vital("vital_memory", kVitalMemorySource)) {
    return;
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
