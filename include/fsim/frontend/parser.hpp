// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/frontend/diagnostic.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
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

}  // namespace fsim::frontend
