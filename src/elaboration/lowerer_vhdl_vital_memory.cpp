// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"
#include "fsim/support/path.hpp"

#include <fstream>

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

std::string_view simple_name(const std::string_view name) {
  const auto separator = name.find_last_of('.');
  return name.substr(
      separator == std::string_view::npos ? 0U : separator + 1U);
}

SourceLocation source_location(const frontend::SourceSpan& span) {
  return SourceLocation{
      span.source_name.str(),
      static_cast<std::uint32_t>(span.begin.line),
      static_cast<std::uint32_t>(span.begin.column)};
}

struct DeclareActuals {
  const frontend::Expression* words{};
  const frontend::Expression* word_width{};
  const frontend::Expression* subword_width{};
  const frontend::Expression* load_file{};
  const frontend::Expression* binary{};
};

}  // namespace

Lowerer::ExpressionAttempt Lowerer::lower_vhdl_vital_memory_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* const expected_type) {
  if (language_ != frontend::Language::Vhdl2008
      || expression.kind != ExpressionKind::Call
      || simple_name(expression.text) != "vitaldeclarememory") {
    return ExpressionAttempt{};
  }
  if (expected_width != 32U || expected_type == nullptr
      || !expected_type->vhdl_access
      || expected_type->spelling != "vitalmemorydatatype") {
    report(
        "FSIM-ELAB-VITALMEM-001",
        "VitalDeclareMemory requires a VitalMemoryDataType result context",
        expression.span);
    return std::nullopt;
  }
  if (expression.operands.size() < 2U
      || expression.operands.size() > 5U) {
    report(
        "FSIM-ELAB-VITALMEM-001",
        "VitalDeclareMemory requires two or three geometry actuals and "
        "optional load-file controls",
        expression.span);
    return std::nullopt;
  }

  bool subword_profile{};
  for (const auto& name : expression.call_argument_names) {
    if (name == "noofbitspersubword") subword_profile = true;
  }
  if (expression.call_argument_names.empty()
      && expression.operands.size() >= 3U
      && !is_string_expression(expression.operands[2])) {
    subword_profile = true;
  }
  DeclareActuals actuals;
  const std::array<std::string_view, 5> with_subword{
      "noofwords", "noofbitsperword", "noofbitspersubword",
      "memoryloadfile", "binaryloadfile"};
  const std::array<std::string_view, 4> without_subword{
      "noofwords", "noofbitsperword", "memoryloadfile",
      "binaryloadfile"};
  std::size_t positional{};
  for (std::size_t index = 0; index < expression.operands.size(); ++index) {
    auto name = expression.call_argument_names.empty()
        ? std::string_view{}
        : std::string_view{expression.call_argument_names[index]};
    if (name.empty()) {
      const auto profile_size = subword_profile
          ? with_subword.size()
          : without_subword.size();
      if (positional >= profile_size) {
        report(
            "FSIM-ELAB-VITALMEM-001",
            "VitalDeclareMemory has too many positional actuals for the "
            "selected profile",
            expression.operands[index].span);
        return std::nullopt;
      }
      name = subword_profile
          ? with_subword[positional++]
          : without_subword[positional++];
    }
    const auto assign = [&](const frontend::Expression*& target) {
      if (target != nullptr) {
        report(
            "FSIM-ELAB-VITALMEM-001",
            "VitalDeclareMemory has a duplicate '" + std::string{name}
                + "' association",
            expression.operands[index].span);
        return false;
      }
      target = &expression.operands[index];
      return true;
    };
    bool accepted = name == "noofwords" ? assign(actuals.words)
        : name == "noofbitsperword" ? assign(actuals.word_width)
        : name == "noofbitspersubword" ? assign(actuals.subword_width)
        : name == "memoryloadfile" ? assign(actuals.load_file)
        : name == "binaryloadfile" ? assign(actuals.binary)
        : false;
    if (!accepted) {
      if (name != "noofwords" && name != "noofbitsperword"
          && name != "noofbitspersubword" && name != "memoryloadfile"
          && name != "binaryloadfile") {
        report(
            "FSIM-ELAB-VITALMEM-001",
            "VitalDeclareMemory has unknown association '"
                + std::string{name} + "'",
            expression.operands[index].span);
      }
      return std::nullopt;
    }
  }
  if (actuals.words == nullptr || actuals.word_width == nullptr
      || (subword_profile && actuals.subword_width == nullptr)) {
    report(
        "FSIM-ELAB-VITALMEM-001",
        "VitalDeclareMemory is missing required geometry actuals",
        expression.span);
    return std::nullopt;
  }
  const auto words = static_integer_value(*actuals.words);
  const auto word_width = static_integer_value(*actuals.word_width);
  const auto subword_width = actuals.subword_width != nullptr
      ? static_integer_value(*actuals.subword_width)
      : word_width;
  if (!words || !word_width || !subword_width
      || *words <= 0 || *word_width <= 0 || *subword_width <= 0
      || *subword_width > *word_width) {
    report(
        "FSIM-ELAB-VITALMEM-002",
        "VitalDeclareMemory geometry must be static and positive, with the "
        "subword no wider than the word",
        expression.span);
    return std::nullopt;
  }

  StringRegisterId load_file{};
  if (actuals.load_file != nullptr) {
    const auto lowered = lower_string_expression(*actuals.load_file);
    if (!lowered) {
      report(
          "FSIM-ELAB-VITALMEM-003",
          "VitalDeclareMemory MemoryLoadFile must be a string expression",
          actuals.load_file->span);
      return std::nullopt;
    }
    load_file = *lowered;
  } else {
    load_file = allocate_string_register();
    process_.operations.emplace_back(LoadStringConstant{load_file, ""});
  }
  bool binary{};
  if (actuals.binary != nullptr) {
    const auto value = actuals.binary->kind == ExpressionKind::BooleanLiteral
        ? std::optional<std::int64_t>{
              actuals.binary->text == "true" ? 1 : 0}
        : static_integer_value(*actuals.binary);
    if (!value || (*value != 0 && *value != 1)) {
      report(
          "FSIM-ELAB-VITALMEM-003",
          "VitalDeclareMemory BinaryLoadFile must be a static Boolean",
          actuals.binary->span);
      return std::nullopt;
    }
    binary = *value != 0;
  }
  bool embedded_load{};
  std::string embedded_load_text;
  if (actuals.load_file != nullptr
      && actuals.load_file->kind == ExpressionKind::StringLiteral
      && actuals.load_file->decoded_string
      && !actuals.load_file->decoded_string->empty()) {
    auto path = fsim::support::path_from_utf8(
        *actuals.load_file->decoded_string);
    if (path.is_relative()) {
      path = fsim::support::path_from_utf8(
                 frontend::physical_source(actuals.load_file->span))
                 .parent_path()
          / path;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      report(
          "FSIM-ELAB-VITALMEM-004",
          "VitalDeclareMemory load file is unavailable: "
              + path.lexically_normal().generic_string(),
          actuals.load_file->span);
      return std::nullopt;
    }
    embedded_load_text.assign(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    if ((!input.good() && !input.eof())
        || embedded_load_text.size() > maximum_memory_file_bytes) {
      report(
          "FSIM-ELAB-VITALMEM-004",
          "VitalDeclareMemory load file is unreadable or exceeds the 1 MiB "
          "input budget",
          actuals.load_file->span);
      return std::nullopt;
    }
    embedded_load = true;
  }
  const auto destination = allocate_register(
      32U, frontend::ValueDomain::Bit2);
  process_.operations.emplace_back(VitalMemoryDeclare{
      destination,
      static_cast<std::uint64_t>(*words),
      static_cast<std::uint64_t>(*word_width),
      static_cast<std::uint64_t>(*subword_width),
      load_file,
      binary,
      embedded_load,
      std::move(embedded_load_text),
      source_location(expression.span)});
  return destination;
}

}  // namespace fsim::elaboration
