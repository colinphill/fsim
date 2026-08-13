// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/frontend/diagnostic.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::frontend {

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
    LexResult lexed,
    bool system_verilog = true);

}  // namespace fsim::frontend
