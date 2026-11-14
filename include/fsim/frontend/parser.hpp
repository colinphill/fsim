// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/frontend/diagnostic.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::frontend {

/// Compiler-recognized members of the VHDL-2019 STD.ENV simulator, data, and
/// time surface. The selected-name spelling is deliberately exact so an
/// unrelated user declaration with the same simple name is never captured.
enum class VhdlSimulatorApi : std::uint8_t {
  none,
  stop,
  finish,
  resolution_limit,
  dayofweek,
  time_record,
  localtime,
  gmtime,
  epoch,
  time_to_seconds,
  seconds_to_time,
  to_string,
  getenv,
  vhdl_version,
  tool_type,
  tool_vendor,
  tool_name,
  tool_edition,
  tool_version,
  call_path_element,
  call_path_vector,
  call_path_vector_ptr,
  get_call_path,
  file_name,
  file_path,
  file_line,
  directory_items,
  directory,
  dir_open_status,
  dir_create_status,
  dir_delete_status,
  file_delete_status,
  dir_open,
  dir_close,
  dir_itemexists,
  dir_itemisdir,
  dir_itemisfile,
  dir_workingdir,
  dir_createdir,
  dir_deletedir,
  dir_deletefile,
  dir_separator,
  psl_assert_failed,
  psl_is_covered,
  get_psl_cover_assert,
  psl_is_assert_covered,
  set_psl_cover_assert,
  clear_psl_state,
  is_vhdl_assert_failed,
  get_vhdl_assert_count,
  clear_vhdl_assert,
  set_vhdl_assert_enable,
  get_vhdl_assert_enable,
  set_vhdl_assert_format,
  get_vhdl_assert_format,
  set_vhdl_read_severity,
  get_vhdl_read_severity,
};

/// The first VHDL revision providing a STD.ENV subprogram. STOP, FINISH and
/// RESOLUTION_LIMIT are IEEE 1076-2008 16.5; the remainder is VHDL-2019.
[[nodiscard]] VhdlStandard vhdl_simulator_api_standard(
    VhdlSimulatorApi api) noexcept;

[[nodiscard]] VhdlSimulatorApi vhdl_simulator_api(
    std::string_view selected_name) noexcept;

/// Concrete compiler-supplied views of the two STD.ENV calendar types. These
/// are direct VHDL-2019 types, not compatibility package declarations.
[[nodiscard]] Type vhdl_environment_dayofweek_type();
[[nodiscard]] Type vhdl_environment_time_record_type();
[[nodiscard]] bool is_vhdl_environment_time_record(
    const Type& type) noexcept;
[[nodiscard]] Type vhdl_environment_directory_type();
[[nodiscard]] Type vhdl_environment_directory_items_type();
[[nodiscard]] Type vhdl_environment_directory_status_type(
    VhdlSimulatorApi kind);
[[nodiscard]] bool is_vhdl_environment_directory(
    const Type& type) noexcept;
[[nodiscard]] Type vhdl_environment_call_path_element_type();
[[nodiscard]] Type vhdl_environment_call_path_vector_type();
[[nodiscard]] Type vhdl_environment_call_path_vector_ptr_type();
[[nodiscard]] bool is_vhdl_environment_call_path_type(
    const Type& type) noexcept;
/// Compiler-supplied exact STD.REFLECTION scalar and access mirror types.
/// These are direct VHDL-2019 type views, not compatibility declarations.
[[nodiscard]] std::optional<Type> vhdl_reflection_type(
    std::string_view simple_name);

struct ParseResult {
  ParsedDesign design;
  std::vector<Diagnostic> diagnostics;

  [[nodiscard]] bool ok() const { return !has_errors(diagnostics); }
};

// What a SystemVerilog package member names, as far as parsing a use of it
// needs (IEEE 1800-2023 26.3).
enum class SystemVerilogPackageMember : std::uint8_t {
  none,
  // A parameter, variable, net, subroutine, enumeration literal, or let.
  value,
  // A data type of packed or scalar shape, or a covergroup.
  type,
  // A data type with unpacked dimensions or a dynamic shape.
  unpacked_type,
  // A class.
  class_type,
};

// Recognize imported names without constructing parser declarations for
// compiled packages. The owning compiler keeps the lookup alive during parse.
using SystemVerilogPackageMemberLookup = std::function<
    SystemVerilogPackageMember(
        std::string_view package, std::string_view member)>;

// The sequence and property declarations of a SystemVerilog package parsed
// or compiled apart from the unit that imports them (IEEE 1800-2023 26.3,
// 16.8, 16.12): those of a package an earlier group parsed as they are, and
// those of a compiled package as their source tokens (name, header, `;`,
// body and end keyword after the `sequence` or `property` keyword in
// `keywords`), which the importing parser parses again.
struct SystemVerilogPackageAssertions {
  std::vector<SystemVerilogAssertionDeclaration> declarations;
  std::vector<std::vector<Token>> sources;
};
using SystemVerilogPackageAssertionLookup = std::function<
    std::optional<SystemVerilogPackageAssertions>(std::string_view package)>;

// The members of parsed packages: those each declares, and those it exports
// from the packages it imports (IEEE 1800-2023 26.3, 26.6). A package not
// held here is answered by a lookup of compiled packages.
class SystemVerilogPackageMembers {
public:
  // Records a parsed package.
  void add(const DesignUnit& package);
  // Records one member of a package being parsed.
  void add_member(std::string_view package, std::string_view member,
      SystemVerilogPackageMember kind);
  [[nodiscard]] SystemVerilogPackageMember find(std::string_view package,
      std::string_view member,
      const SystemVerilogPackageMemberLookup& fallback) const;
  // The sequence and property declarations of a recorded package, else the
  // fallback's answer.
  [[nodiscard]] std::optional<SystemVerilogPackageAssertions> assertions(
      std::string_view package,
      const SystemVerilogPackageAssertionLookup& fallback) const;

private:
  struct Package {
    std::unordered_map<std::string, SystemVerilogPackageMember> members;
    std::vector<SystemVerilogAssertionDeclaration> assertion_declarations;
    std::vector<SystemVerilogImport> imports;
    std::vector<SystemVerilogExport> exports;
    std::unordered_set<std::string> wildcard_references;
  };
  [[nodiscard]] SystemVerilogPackageMember find(std::string_view package,
      std::string_view member,
      const SystemVerilogPackageMemberLookup& fallback,
      std::vector<std::string_view>& visiting) const;
  std::unordered_map<std::string, Package> packages_;
};

// The names IEEE fixed_generic_pkg and float_generic_pkg (and their
// fixed_pkg and float_pkg instances) export as compiler intrinsics; empty
// for any other package.
[[nodiscard]] std::vector<std::string> vhdl_ieee_intrinsic_package_declarations(
    std::string_view package);

[[nodiscard]] ParseResult parse(SourceText source, Language language,
    VhdlStandard vhdl_standard = VhdlStandard::Vhdl2008);
[[nodiscard]] ParseResult parse_text(std::string_view source_name,
    std::string_view text,
    Language language,
    VhdlStandard vhdl_standard = VhdlStandard::Vhdl2008);
[[nodiscard]] ParseResult parse_file(const std::filesystem::path& path,
    Language language,
    VhdlStandard vhdl_standard = VhdlStandard::Vhdl2008);
[[nodiscard]] ParseResult parse_file(const std::filesystem::path& path);
[[nodiscard]] std::optional<Language> infer_language(
    const std::filesystem::path& path) noexcept;
[[nodiscard]] ParseResult parse_vhdl(SourceText source,
    VhdlStandard vhdl_standard = VhdlStandard::Vhdl2008);
// User-defined VHDL-2019 conditional analysis identifiers and their values
// (IEEE 1076-2019 24.2).
using VhdlConditionalIdentifiers
    = std::vector<std::pair<std::string, std::string>>;
[[nodiscard]] ParseResult parse_vhdl(SourceText source,
    VhdlStandard vhdl_standard,
    const VhdlConditionalIdentifiers& identifiers);
[[nodiscard]] ParseResult parse_verilog(SourceText source,
                                         bool system_verilog = true);
[[nodiscard]] ParseResult parse_verilog(
    SourceText source,
    StandardRevision standard_revision);
[[nodiscard]] ParseResult parse_verilog(
    SourceText source,
    StandardRevision standard_revision,
    std::string_view compatibility_profile);
[[nodiscard]] ParseResult parse_verilog(
    LexResult lexed,
    bool system_verilog = true);
[[nodiscard]] ParseResult parse_verilog(
    LexResult lexed,
    StandardRevision standard_revision);
[[nodiscard]] ParseResult parse_verilog(
    LexResult lexed,
    StandardRevision standard_revision,
    std::string_view compatibility_profile);
[[nodiscard]] ParseResult parse_verilog(
    LexResult lexed,
    StandardRevision standard_revision,
    std::string_view compatibility_profile,
    SystemVerilogPackageMemberLookup package_member_lookup);
[[nodiscard]] ParseResult parse_verilog(
    LexResult lexed,
    StandardRevision standard_revision,
    std::string_view compatibility_profile,
    SystemVerilogPackageMemberLookup package_member_lookup,
    SystemVerilogPackageAssertionLookup package_assertion_lookup);

// The statement `run_test(name)` of the UVM library expanded onto the native
// UVM services (IEEE 1800.2-2020 F.3.1.2): the phases before and after run
// are native calls; the run phase forks each component's run_phase as a
// process of the calling thread, which ends the simulation when the phase
// ends. The block is parsed onto the call's source line, so it reports
// there. On failure `diagnostic` holds the parser's first message.
// The library declarations the block names, as the caller resolved them by
// their recorded roles (support::UvmRole): the package-qualified names of
// uvm_phase and uvm_component, and the component's phase methods in the
// order build, connect, end_of_elaboration, start_of_simulation, run,
// extract, check, report, final.
struct UvmRunTestNames {
    std::string phase_class;
    std::string component_class;
    std::array<std::string, 9U> phase_methods;
};
[[nodiscard]] std::optional<Statement> uvm_run_test_statement(
    const SourceSpan& call, std::optional<Expression> test_name,
    const UvmRunTestNames& names,
    StandardRevision standard_revision, std::string& diagnostic);

}  // namespace fsim::frontend
