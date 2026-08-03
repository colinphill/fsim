// SPDX-License-Identifier: Apache-2.0
#include "fsim/project/project.hpp"
#include "fsim/support/path.hpp"

#include <cctype>
#include <fstream>
#include <iterator>
#include <string>

namespace fsim::project {
namespace {

std::string_view trim(const std::string_view value) {
  std::size_t begin = 0;
  while (begin < value.size()
         && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }
  std::size_t end = value.size();
  while (end > begin
         && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  return value.substr(begin, end - begin);
}

}  // namespace

std::optional<std::string> migrate_to_schema_2(
    const std::filesystem::path& manifest,
    diagnostic::Engine& diagnostics) {
  const auto source_name = support::path_to_utf8(manifest);
  std::ifstream input(manifest, std::ios::binary);
  if (!input) {
    diagnostics.error(
        "FSIM-PROJ-0010",
        "cannot open project manifest for migration: " + source_name);
    return std::nullopt;
  }
  std::string source{
      std::istreambuf_iterator<char>{input},
      std::istreambuf_iterator<char>{}};
  if (!input.eof() && input.fail()) {
    diagnostics.error(
        "FSIM-PROJ-0010",
        "cannot read project manifest for migration: " + source_name);
    return std::nullopt;
  }

  std::size_t line_begin = 0;
  while (line_begin <= source.size()) {
    const auto line_end = source.find('\n', line_begin);
    const auto end = line_end == std::string::npos ? source.size() : line_end;
    const std::string_view line{source.data() + line_begin, end - line_begin};
    const auto content_end = line.find('#');
    const auto content = trim(line.substr(0, content_end));
    const auto equals = content.find('=');
    if (equals != std::string_view::npos
        && trim(content.substr(0, equals)) == "schema") {
      const auto value = trim(content.substr(equals + 1));
      if (value != "1" && value != "2") {
        diagnostics.error(
            "FSIM-PROJ-0011",
            "cannot migrate project schema '" + std::string{value}
                + "'; expected schema 1 or 2");
        return std::nullopt;
      }
      if (value == "1") {
        const auto value_offset = static_cast<std::size_t>(
            value.data() - source.data());
        source.replace(value_offset, value.size(), "2");
      }
      return source;
    }
    if (line_end == std::string::npos) {
      break;
    }
    line_begin = line_end + 1;
  }
  diagnostics.error(
      "FSIM-PROJ-0011",
      "cannot migrate a manifest without a top-level schema declaration");
  return std::nullopt;
}

}  // namespace fsim::project
