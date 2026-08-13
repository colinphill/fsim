// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/parser.hpp"
#include "fsim/frontend/preprocessor.hpp"

#include <cctype>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>

namespace fsim::frontend {

ParseResult parse(SourceText source, Language language,
    VhdlStandard vhdl_standard)
{
    switch (language) {
    case Language::Vhdl2008:
        return parse_vhdl(std::move(source), vhdl_standard);
    case Language::Verilog2005:
      return parse_verilog(std::move(source), false);
    case Language::SystemVerilog2017:
      return parse_verilog(std::move(source), true);
    }
  return {};
}

ParseResult parse_text(std::string_view source_name, std::string_view text,
    Language language, VhdlStandard vhdl_standard)
{
    return parse(SourceText { std::string(source_name), std::string(text) },
        language, vhdl_standard);
}

ParseResult parse_file(const std::filesystem::path& path,
    Language language, VhdlStandard vhdl_standard)
{
    if (language == Language::Verilog2005
        || language == Language::SystemVerilog2017) {
        auto preprocessed = preprocess_verilog_file(path, language);
        return parse_verilog(
            std::move(preprocessed.lexed),
            language == Language::SystemVerilog2017);
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        ParseResult result;
        result.diagnostics.push_back(Diagnostic {
            DiagnosticSeverity::Error,
            "FSIM-FE-IO-001",
            "unable to open source file",
            SourceSpan {
                path.string(), SourceLocation { }, SourceLocation { },
                path.string(), { } },
            { },
        });
        return result;
    }

    std::string text { std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>() };
    if (!input.good() && !input.eof()) {
        ParseResult result;
        result.diagnostics.push_back(Diagnostic {
            DiagnosticSeverity::Error,
            "FSIM-FE-IO-002",
            "failed while reading source file",
            SourceSpan {
                path.string(), SourceLocation { }, SourceLocation { },
                path.string(), { } },
            { },
        });
        return result;
    }
    return parse(SourceText { path.string(), std::move(text) }, language,
        vhdl_standard);
}

std::optional<Language> infer_language(
    const std::filesystem::path& path) noexcept {
  auto extension = path.extension().string();
  for (char& character : extension) {
    character = static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  }
  if (extension == ".vhd" || extension == ".vhdl") {
    return Language::Vhdl2008;
  }
  if (extension == ".v") {
    return Language::Verilog2005;
  }
  if (extension == ".sv" || extension == ".svh") {
    return Language::SystemVerilog2017;
  }
  return std::nullopt;
}

ParseResult parse_file(const std::filesystem::path& path) {
  const auto language = infer_language(path);
  if (!language) {
    ParseResult result;
    result.diagnostics.push_back(Diagnostic{
        DiagnosticSeverity::Error,
        "FSIM-FE-IO-003",
        "cannot infer HDL language from file extension",
        SourceSpan{
            path.string(), SourceLocation{}, SourceLocation{},
            path.string(), {}},
        {},
    });
    return result;
  }
  return parse_file(path, *language);
}

}  // namespace fsim::frontend
