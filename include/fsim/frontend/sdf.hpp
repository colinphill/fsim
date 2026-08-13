// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/diagnostic.hpp"
#include "fsim/frontend/source.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::frontend {

enum class SdfTokenKind {
  LeftParenthesis,
  RightParenthesis,
  Colon,
  Plus,
  Minus,
  Star,
  Slash,
  Dot,
  Keyword,
  Identifier,
  EscapedIdentifier,
  String,
  Number,
  Comment,
  EndOfFile,
};

struct SdfLexerLimits {
  std::size_t max_source_bytes{64U * 1024U * 1024U};
  std::size_t max_tokens{1'000'000U};
  std::size_t max_parenthesis_depth{4096U};
  std::size_t max_token_bytes{1024U * 1024U};
  std::size_t max_numeric_bytes{4096U};

  friend constexpr bool operator==(const SdfLexerLimits&,
                                   const SdfLexerLimits&) = default;
};

struct SdfToken {
  SdfTokenKind kind{SdfTokenKind::EndOfFile};
  std::string spelling;
  SourceSpan span;

  friend bool operator==(const SdfToken&, const SdfToken&) = default;
};

struct SdfLexResult {
  std::vector<SdfToken> tokens;
  std::vector<Diagnostic> diagnostics;
  bool resource_exhausted{};

  [[nodiscard]] bool ok() const { return !has_errors(diagnostics); }
};

enum class SdfRevision {
  Sdf21,
  Sdf30,
  Sdf40,
};

enum class SdfRevisionAdapter {
  None,
  Sdf21,
  Sdf30,
};

enum class SdfHeaderKind {
  SdfVersion,
  Design,
  Date,
  Vendor,
  Program,
  ProgramVersion,
  Divider,
  Voltage,
  Process,
  Temperature,
  Timescale,
};

enum class SdfConstructKind {
  Cell,
  CellType,
  Instance,
  Correlation,
  Delay,
  Absolute,
  Increment,
  Conditional,
  ConditionalElse,
  Iopath,
  Retain,
  Interconnect,
  NetDelay,
  Port,
  Device,
  PathPulse,
  PathPulsePercent,
  Mipd,
  TimingCheck,
  Negative,
  Setup,
  Hold,
  SetupHold,
  Recovery,
  Removal,
  RecRem,
  Skew,
  BidirectSkew,
  Width,
  Period,
  NoChange,
  TimingEnvironment,
  PathConstraint,
  PeriodConstraint,
  SkewConstraint,
  Sum,
  Diff,
  Arrival,
  Departure,
  Slack,
  Waveform,
  Exception,
  Label,
  Name,
  LabelEntry,
  ConstraintPath,
  StampCondition,
  CheckCondition,
  ConditionExpression,
  Edge,
  Value,
  Unknown,
};

enum class SdfInstanceSelectorKind {
  Empty,
  Exact,
  Wildcard,
};

enum class SdfExactValueKind {
  Empty,
  Scalar,
  Triple,
};

enum class SdfTimeUnit {
  Second,
  Millisecond,
  Microsecond,
  Nanosecond,
  Picosecond,
  Femtosecond,
};

enum class SdfExactConversionError {
  None,
  Overflow,
  Lossy,
};

struct SdfExactDecimal {
  bool negative{};
  std::string coefficient{"0"};
  std::int64_t exponent10{};
  std::string canonical{"0e0"};

  friend bool operator==(const SdfExactDecimal&,
                         const SdfExactDecimal&) = default;
};

struct SdfExactValue {
  SdfExactValueKind kind{SdfExactValueKind::Empty};
  std::array<std::optional<SdfExactDecimal>, 3> components;
  std::string canonical;

  friend bool operator==(const SdfExactValue&,
                         const SdfExactValue&) = default;
};

struct SdfNormalizedName {
  std::vector<std::string> segments;
  std::string canonical;

  friend bool operator==(const SdfNormalizedName&,
                         const SdfNormalizedName&) = default;
};

struct SdfNormalizedTimescale {
  SdfTimeUnit unit{SdfTimeUnit::Nanosecond};
  SdfExactDecimal magnitude;
  SdfExactDecimal femtoseconds;
  std::string canonical;

  friend bool operator==(const SdfNormalizedTimescale&,
                         const SdfNormalizedTimescale&) = default;
};

struct SdfExactIntegerResult {
  SdfExactConversionError error{SdfExactConversionError::None};
  std::string value;

  [[nodiscard]] bool ok() const noexcept {
    return error == SdfExactConversionError::None;
  }

  friend bool operator==(const SdfExactIntegerResult&,
                         const SdfExactIntegerResult&) = default;
};

struct SdfSyntaxAtom {
  SdfTokenKind kind{SdfTokenKind::Identifier};
  std::string spelling;
  SourceSpan span;

  friend bool operator==(const SdfSyntaxAtom&,
                         const SdfSyntaxAtom&) = default;
};

struct SdfSyntaxNode {
  SdfConstructKind kind{SdfConstructKind::Unknown};
  std::string keyword_spelling;
  std::vector<SdfSyntaxAtom> atoms;
  std::vector<SdfSyntaxNode> children;
  std::size_t first_token{};
  std::size_t token_count{};
  SourceSpan span;
  std::optional<SdfExactValue> exact_value;
  std::optional<SdfExactValue> scaled_femtoseconds;
  std::vector<std::string> canonical_atoms;
  std::string canonical_identity;

  friend bool operator==(const SdfSyntaxNode&,
                         const SdfSyntaxNode&) = default;
};

struct SdfCell {
  std::string cell_type;
  std::string cell_type_spelling;
  SdfInstanceSelectorKind instance_kind{SdfInstanceSelectorKind::Empty};
  std::string instance_spelling;
  std::vector<std::string> instance_spellings;
  bool wildcard_requires_physical_primitive{};
  std::vector<SdfSyntaxNode> declarations;
  SourceSpan span;
  std::optional<SdfNormalizedName> normalized_instance;

  friend bool operator==(const SdfCell&, const SdfCell&) = default;
};

struct SdfHeaderRecord {
  SdfHeaderKind kind{SdfHeaderKind::SdfVersion};
  std::string keyword_spelling;
  std::vector<std::string> value_spellings;
  std::string canonical_value;
  SourceSpan span;
  std::optional<SdfExactValue> exact_value;

  friend bool operator==(const SdfHeaderRecord&,
                         const SdfHeaderRecord&) = default;
};

struct SdfRawForm {
  std::string keyword_spelling;
  std::size_t first_token{};
  std::size_t token_count{};
  SourceSpan span;

  friend bool operator==(const SdfRawForm&, const SdfRawForm&) = default;
};

struct SdfIrLimits {
  std::size_t max_cells{1'000'000U};
  std::size_t max_nodes{1'000'000U};
  std::size_t max_identity_bytes{64U * 1024U * 1024U};

  friend constexpr bool operator==(const SdfIrLimits&,
                                   const SdfIrLimits&) = default;
};

struct SdfIrCell {
  std::uint64_t id{};
  std::string cell_type;
  SdfInstanceSelectorKind instance_kind{SdfInstanceSelectorKind::Empty};
  std::optional<SdfNormalizedName> instance;
  bool wildcard_requires_physical_primitive{};
  std::size_t first_node{};
  std::size_t node_count{};
  SourceSpan span;
  std::string source_identity;
  std::string canonical_identity;

  friend bool operator==(const SdfIrCell&, const SdfIrCell&) = default;
};

struct SdfIrNode {
  std::uint64_t id{};
  std::uint64_t cell_id{};
  std::uint64_t parent_id{};
  std::size_t sibling_index{};
  std::size_t depth{};
  SdfConstructKind kind{SdfConstructKind::Unknown};
  std::vector<std::string> canonical_atoms;
  std::optional<SdfExactValue> exact_value;
  std::optional<SdfExactValue> scaled_femtoseconds;
  std::size_t first_token{};
  std::size_t token_count{};
  SourceSpan span;
  std::string source_identity;
  std::string profile_identity;
  std::string canonical_identity;

  friend bool operator==(const SdfIrNode&, const SdfIrNode&) = default;
};

struct SdfFile;

class SdfIr {
public:
  static constexpr std::uint32_t schema_version = 1U;

  [[nodiscard]] SdfRevision revision() const noexcept;
  [[nodiscard]] SdfRevisionAdapter revision_adapter() const noexcept;
  [[nodiscard]] std::string_view source_name() const noexcept;
  [[nodiscard]] const std::optional<SdfNormalizedTimescale>& timescale() const
      noexcept;
  [[nodiscard]] std::span<const SdfIrCell> cells() const noexcept;
  [[nodiscard]] std::span<const SdfIrNode> nodes() const noexcept;
  [[nodiscard]] const SdfIrCell* find_cell(std::uint64_t id) const noexcept;
  [[nodiscard]] const SdfIrNode* find_node(std::uint64_t id) const noexcept;
  [[nodiscard]] std::string_view semantic_identity() const noexcept;
  [[nodiscard]] std::string describe() const;
  [[nodiscard]] bool semantically_equal(const SdfIr& other) const noexcept;

  friend bool operator==(const SdfIr& left, const SdfIr& right) noexcept {
    return left.semantically_equal(right);
  }

  SdfIr(SdfRevision revision, SdfRevisionAdapter adapter,
        std::string source_name,
        std::optional<SdfNormalizedTimescale> timescale,
        std::vector<SdfIrCell> cells, std::vector<SdfIrNode> nodes,
        std::string semantic_identity);

private:
  SdfRevision revision_{SdfRevision::Sdf40};
  SdfRevisionAdapter revision_adapter_{SdfRevisionAdapter::None};
  std::string source_name_;
  std::optional<SdfNormalizedTimescale> timescale_;
  std::vector<SdfIrCell> cells_;
  std::vector<SdfIrNode> nodes_;
  std::string semantic_identity_;

};

struct SdfFile {
  std::string source_name;
  SdfRevision revision{SdfRevision::Sdf40};
  SdfRevisionAdapter revision_adapter{SdfRevisionAdapter::None};
  bool has_revision{};
  std::vector<SdfHeaderRecord> headers;
  std::vector<SdfRawForm> body_forms;
  std::vector<SdfCell> cells;
  std::vector<SdfToken> tokens;
  SourceSpan span;
  std::optional<SdfNormalizedTimescale> normalized_timescale;
  std::shared_ptr<const SdfIr> normalized_ir;

  [[nodiscard]] const SdfHeaderRecord* find_header(
      SdfHeaderKind kind) const noexcept;
};

struct SdfParseResult {
  SdfFile file;
  std::vector<Diagnostic> diagnostics;
  bool resource_exhausted{};

  [[nodiscard]] bool ok() const { return !has_errors(diagnostics); }
};

[[nodiscard]] SdfLexResult lex_sdf(
    SourceText source, SdfLexerLimits limits = {});
[[nodiscard]] SdfParseResult parse_sdf(
    SourceText source, SdfLexerLimits limits = {});
void normalize_sdf(SdfFile& file, std::vector<Diagnostic>& diagnostics);
void lower_sdf_ir(SdfFile& file, std::vector<Diagnostic>& diagnostics,
                  SdfIrLimits limits = {});
[[nodiscard]] SdfExactIntegerResult sdf_exact_integer_at(
    const SdfExactDecimal& value, std::int64_t target_exponent10,
    std::size_t max_digits = 4096U);
[[nodiscard]] bool is_sdf_keyword(std::string_view spelling) noexcept;
[[nodiscard]] const char* to_string(SdfTokenKind kind) noexcept;
[[nodiscard]] const char* to_string(SdfRevision revision) noexcept;
[[nodiscard]] const char* to_string(SdfRevisionAdapter adapter) noexcept;
[[nodiscard]] const char* to_string(SdfHeaderKind kind) noexcept;
[[nodiscard]] const char* to_string(SdfConstructKind kind) noexcept;
[[nodiscard]] const char* to_string(SdfInstanceSelectorKind kind) noexcept;
[[nodiscard]] const char* to_string(SdfExactValueKind kind) noexcept;
[[nodiscard]] const char* to_string(SdfTimeUnit unit) noexcept;
[[nodiscard]] const char* to_string(SdfExactConversionError error) noexcept;

}  // namespace fsim::frontend
