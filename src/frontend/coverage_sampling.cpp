// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_sampling.hpp"
#include "fsim/frontend/coverage_limits.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <string>

namespace fsim::frontend {
namespace {

std::string bin_identity(
    const SystemVerilogCovergroupDeclaration& declaration,
    const SystemVerilogCoverageDeclaration& coverpoint,
    const SystemVerilogCoverageBin& bin,
    const std::optional<std::int64_t> automatic_value) {
  auto identity = declaration.canonical_identity + "::" + coverpoint.name
      + "." + bin.name;
  if (automatic_value) {
    identity += "[" + std::to_string(*automatic_value) + "]";
  }
  return identity;
}

bool record_hit(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration,
    const SystemVerilogCoverageDeclaration& coverpoint,
    const SystemVerilogCoverageBin& bin,
    const std::int64_t value,
    SystemVerilogCoverageSampleResult& result,
    std::vector<Diagnostic>& diagnostics) {
  const auto automatic_value =
      bin.selection == SystemVerilogCoverageBinSelection::Automatic
      ? std::optional<std::int64_t>{value} : std::nullopt;
  auto identity = bin_identity(
      declaration, coverpoint, bin, automatic_value);
  auto hit = std::ranges::find(
      instance.bin_hits, identity, &SystemVerilogCoverageBinHit::identity);
  if (hit == instance.bin_hits.end()) {
    SystemVerilogCoverageBinHit created;
    created.coverage_declaration_index = coverpoint.declaration_index;
    created.bin_declaration_index = bin.declaration_index;
    created.identity = identity;
    created.automatic_value = automatic_value;
    created.hit_count = 1U;
    created.at_least = bin.at_least;
    created.covered = created.hit_count >= created.at_least;
    result.threshold_reached = created.covered;
    instance.bin_hits.push_back(std::move(created));
  } else {
    if (hit->hit_count == std::numeric_limits<std::uint64_t>::max()) {
      result.hit_count_overflow = true;
      diagnostics.push_back(Diagnostic{
          DiagnosticSeverity::Error,
          "FSIM-SV-COV-002",
          "coverage hit count overflow for bin '" + identity + "'",
          bin.span,
          {}});
      return false;
    }
    ++hit->hit_count;
    hit->covered = hit->hit_count >= hit->at_least;
    result.threshold_reached = hit->covered;
  }
  result.hit_bin_identities.push_back(std::move(identity));
  return true;
}

bool values_match(
    const std::vector<SystemVerilogCoverageBinValue>& values,
    const SystemVerilogCoverageSampleValue& sample) {
  return std::ranges::any_of(
      values,
      [&](const SystemVerilogCoverageBinValue& candidate) {
        if (sample.unknown_mask == 0U
            && candidate.exact_value == sample.value) {
          return true;
        }
        if (candidate.range_left && candidate.range_right) {
          const auto lower = std::min(
              *candidate.range_left, *candidate.range_right);
          const auto upper = std::max(
              *candidate.range_left, *candidate.range_right);
          if (sample.unknown_mask == 0U
              && sample.value >= lower && sample.value <= upper) {
            return true;
          }
        }
        if (candidate.wildcard) {
          const auto sampled = static_cast<std::uint64_t>(sample.value);
          if ((sample.unknown_mask & candidate.wildcard_mask) != 0U) {
            return false;
          }
          return (sampled & candidate.wildcard_mask)
              == (candidate.wildcard_value & candidate.wildcard_mask);
        }
        return false;
      });
}

bool exact_match(
    const SystemVerilogCoverageBin& bin,
    const SystemVerilogCoverageSampleValue& value) {
  return values_match(bin.values, value);
}

enum class GuardTruth { False, True, Unknown };

struct GuardOperand {
  std::int64_t value{};
  bool known{};
};

std::span<const Token> strip_guard_parentheses(
    std::span<const Token> tokens) {
  while (tokens.size() >= 2U
         && tokens.front().kind == TokenKind::LeftParen
         && tokens.back().kind == TokenKind::RightParen) {
    int depth{};
    bool encloses_all{true};
    for (std::size_t index = 0; index < tokens.size(); ++index) {
      if (tokens[index].kind == TokenKind::LeftParen) ++depth;
      if (tokens[index].kind == TokenKind::RightParen) --depth;
      if (depth == 0 && index + 1U != tokens.size()) {
        encloses_all = false;
        break;
      }
    }
    if (!encloses_all) break;
    tokens = tokens.subspan(1U, tokens.size() - 2U);
  }
  return tokens;
}

std::optional<std::size_t> guard_operator(
    const std::span<const Token> tokens,
    const TokenKind sought) {
  int parentheses{};
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    if (tokens[index].kind == TokenKind::LeftParen) ++parentheses;
    if (tokens[index].kind == TokenKind::RightParen) --parentheses;
    if (tokens[index].kind == sought && parentheses == 0) return index;
  }
  return std::nullopt;
}

GuardOperand guard_operand(
    std::span<const Token> tokens,
    const SystemVerilogCoverageSampleValue& sample) {
  tokens = strip_guard_parentheses(tokens);
  if (tokens.size() == 1U && tokens.front().kind == TokenKind::Identifier) {
    return {sample.value, sample.unknown_mask == 0U};
  }
  bool negative{};
  if (tokens.size() == 2U && tokens.front().kind == TokenKind::Minus) {
    negative = true;
    tokens = tokens.subspan(1U);
  }
  if (tokens.size() != 1U || tokens.front().kind != TokenKind::Number) {
    return {};
  }
  std::string spelling = tokens.front().text;
  spelling.erase(
      std::remove(spelling.begin(), spelling.end(), '_'), spelling.end());
  std::int64_t value{};
  const auto parsed = std::from_chars(
      spelling.data(), spelling.data() + spelling.size(), value);
  if (parsed.ec != std::errc{}
      || parsed.ptr != spelling.data() + spelling.size()) {
    return {};
  }
  return {negative ? -value : value, true};
}

GuardTruth evaluate_guard(
    std::span<const Token> tokens,
    const SystemVerilogCoverageSampleValue& sample) {
  tokens = strip_guard_parentheses(tokens);
  if (tokens.empty()) return GuardTruth::True;
  if (const auto operation = guard_operator(tokens, TokenKind::OrOr)) {
    const auto left = evaluate_guard(tokens.first(*operation), sample);
    const auto right = evaluate_guard(tokens.subspan(*operation + 1U), sample);
    if (left == GuardTruth::True || right == GuardTruth::True) {
      return GuardTruth::True;
    }
    return left == GuardTruth::Unknown || right == GuardTruth::Unknown
        ? GuardTruth::Unknown : GuardTruth::False;
  }
  if (const auto operation = guard_operator(tokens, TokenKind::AndAnd)) {
    const auto left = evaluate_guard(tokens.first(*operation), sample);
    const auto right = evaluate_guard(tokens.subspan(*operation + 1U), sample);
    if (left == GuardTruth::False || right == GuardTruth::False) {
      return GuardTruth::False;
    }
    return left == GuardTruth::Unknown || right == GuardTruth::Unknown
        ? GuardTruth::Unknown : GuardTruth::True;
  }
  if (tokens.front().text == "!") {
    const auto inner = evaluate_guard(tokens.subspan(1U), sample);
    return inner == GuardTruth::True
        ? GuardTruth::False
        : inner == GuardTruth::False
            ? GuardTruth::True : GuardTruth::Unknown;
  }
  constexpr TokenKind comparisons[] = {
      TokenKind::EqualEqual, TokenKind::CaseEqual, TokenKind::NotEqual,
      TokenKind::CaseNotEqual, TokenKind::Less, TokenKind::LessEqual,
      TokenKind::Greater, TokenKind::GreaterEqual};
  for (const auto comparison : comparisons) {
    const auto operation = guard_operator(tokens, comparison);
    if (!operation) continue;
    const auto left = guard_operand(tokens.first(*operation), sample);
    const auto right = guard_operand(tokens.subspan(*operation + 1U), sample);
    if (!left.known || !right.known) return GuardTruth::Unknown;
    bool result{};
    if (comparison == TokenKind::EqualEqual
        || comparison == TokenKind::CaseEqual) {
      result = left.value == right.value;
    } else if (
        comparison == TokenKind::NotEqual
        || comparison == TokenKind::CaseNotEqual) {
      result = left.value != right.value;
    } else if (comparison == TokenKind::Less) {
      result = left.value < right.value;
    } else if (comparison == TokenKind::LessEqual) {
      result = left.value <= right.value;
    } else if (comparison == TokenKind::Greater) {
      result = left.value > right.value;
    } else {
      result = left.value >= right.value;
    }
    return result ? GuardTruth::True : GuardTruth::False;
  }
  const auto operand = guard_operand(tokens, sample);
  if (!operand.known) return GuardTruth::Unknown;
  return operand.value != 0 ? GuardTruth::True : GuardTruth::False;
}

bool guard_allows(
    const std::vector<Token>& tokens,
    const SystemVerilogCoverageSampleValue& sample) {
  return tokens.empty()
      || evaluate_guard(tokens, sample) == GuardTruth::True;
}

void append_unique_progress(
    std::vector<SystemVerilogCoverageTransitionProgress>& progress,
    SystemVerilogCoverageTransitionProgress candidate) {
  const auto duplicate = std::ranges::any_of(
      progress,
      [&](const SystemVerilogCoverageTransitionProgress& existing) {
        return existing.coverage_declaration_index
                == candidate.coverage_declaration_index
            && existing.bin_declaration_index
                == candidate.bin_declaration_index
            && existing.sequence_index == candidate.sequence_index
            && existing.step_index == candidate.step_index
            && existing.repetition_count == candidate.repetition_count
            && existing.samples_since_step == candidate.samples_since_step;
      });
  if (!duplicate) progress.push_back(std::move(candidate));
}

bool advance_transition_bin(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCoverageDeclaration& coverpoint,
    const SystemVerilogCoverageBin& bin,
    const SystemVerilogCoverageSampleValue& value) {
  std::vector<SystemVerilogCoverageTransitionProgress> retained;
  std::vector<SystemVerilogCoverageTransitionProgress> active;
  for (const auto& progress : instance.transition_progress) {
    if (progress.coverage_declaration_index
            == coverpoint.declaration_index
        && progress.bin_declaration_index == bin.declaration_index) {
      active.push_back(progress);
    } else {
      retained.push_back(progress);
    }
  }

  bool completed{};
  for (const auto& progress : active) {
    if (progress.sequence_index >= bin.transitions.size()) continue;
    const auto& sequence = bin.transitions[progress.sequence_index];
    if (progress.step_index >= sequence.steps.size()) continue;
    const auto& step = sequence.steps[progress.step_index];
    const auto maximum = step.repetition.maximum.value_or(65'536U);
    const auto repeat_kind = step.repetition.kind;
    const bool repeating = repeat_kind
        != SystemVerilogCoverageTransitionRepetitionKind::None;
    const bool step_matches = values_match(step.values, value);
    if (repeating && step_matches
        && progress.repetition_count < maximum) {
      auto repeated = progress;
      ++repeated.repetition_count;
      repeated.samples_since_step = 0U;
      if (repeated.step_index + 1U == sequence.steps.size()
          && repeated.repetition_count >= step.repetition.minimum) {
        completed = true;
      } else {
        append_unique_progress(retained, std::move(repeated));
      }
    }

    const bool repetition_satisfied =
        progress.repetition_count >= step.repetition.minimum;
    if (progress.step_index + 1U < sequence.steps.size()
        && repetition_satisfied) {
      const auto age = progress.samples_since_step + 1U;
      const auto& delay = sequence.delays[progress.step_index];
      const bool nonconsecutive = repeat_kind
          == SystemVerilogCoverageTransitionRepetitionKind::Nonconsecutive;
      const bool within_delay = age >= delay.minimum
          && (nonconsecutive || age <= delay.maximum);
      const auto& next_step = sequence.steps[progress.step_index + 1U];
      if (within_delay && values_match(next_step.values, value)) {
        auto advanced = progress;
        ++advanced.step_index;
        advanced.repetition_count = 1U;
        advanced.samples_since_step = 0U;
        if (advanced.step_index + 1U == sequence.steps.size()
            && advanced.repetition_count
                >= next_step.repetition.minimum) {
          completed = true;
        } else {
          append_unique_progress(retained, std::move(advanced));
        }
      }
      const bool wait_for_delay = age < delay.maximum;
      const bool wait_for_more_repetitions = repeating
          && repeat_kind
              != SystemVerilogCoverageTransitionRepetitionKind::Consecutive
          && progress.repetition_count < maximum;
      if (wait_for_delay || nonconsecutive || wait_for_more_repetitions) {
        auto waiting = progress;
        waiting.samples_since_step = age;
        append_unique_progress(retained, std::move(waiting));
      }
    } else if (repeating
               && repeat_kind
                   != SystemVerilogCoverageTransitionRepetitionKind::Consecutive
               && progress.repetition_count < maximum) {
      auto waiting = progress;
      ++waiting.samples_since_step;
      append_unique_progress(retained, std::move(waiting));
    }
  }

  for (std::size_t sequence_index = 0;
       sequence_index < bin.transitions.size(); ++sequence_index) {
    const auto& sequence = bin.transitions[sequence_index];
    if (sequence.steps.empty()
        || !values_match(sequence.steps.front().values, value)) {
      continue;
    }
    SystemVerilogCoverageTransitionProgress started;
    started.coverage_declaration_index = coverpoint.declaration_index;
    started.bin_declaration_index = bin.declaration_index;
    started.sequence_index = sequence_index;
    started.repetition_count = 1U;
    append_unique_progress(retained, std::move(started));
  }
  instance.transition_progress = std::move(retained);
  return completed;
}

void reset_transition_bin(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCoverageDeclaration& coverpoint,
    const SystemVerilogCoverageBin& bin) {
  std::erase_if(
      instance.transition_progress,
      [&](const SystemVerilogCoverageTransitionProgress& progress) {
        return progress.coverage_declaration_index
                == coverpoint.declaration_index
            && progress.bin_declaration_index == bin.declaration_index;
      });
}

bool cross_intersect_match(
    const std::span<const Token> tokens,
    const SystemVerilogCoverageSampleValue& sample) {
  if (sample.unknown_mask != 0U || tokens.size() < 3U
      || tokens.front().kind != TokenKind::LeftBrace
      || tokens.back().kind != TokenKind::RightBrace) {
    return false;
  }
  const auto values = tokens.subspan(1U, tokens.size() - 2U);
  std::size_t begin{};
  int brackets{};
  for (std::size_t index = 0; index <= values.size(); ++index) {
    const bool separator = index == values.size()
        || (values[index].kind == TokenKind::Comma && brackets == 0);
    if (!separator) {
      if (values[index].kind == TokenKind::LeftBracket) ++brackets;
      if (values[index].kind == TokenKind::RightBracket) --brackets;
      continue;
    }
    const auto candidate = values.subspan(begin, index - begin);
    if (candidate.size() == 1U) {
      const auto operand = guard_operand(candidate, sample);
      if (operand.known && operand.value == sample.value) return true;
    } else if (candidate.size() == 5U
               && candidate.front().kind == TokenKind::LeftBracket
               && candidate[2].kind == TokenKind::Colon
               && candidate.back().kind == TokenKind::RightBracket) {
      const auto left = guard_operand(candidate.subspan(1U, 1U), sample);
      const auto right = guard_operand(candidate.subspan(3U, 1U), sample);
      if (left.known && right.known) {
        const auto lower = std::min(left.value, right.value);
        const auto upper = std::max(left.value, right.value);
        if (sample.value >= lower && sample.value <= upper) return true;
      }
    }
    begin = index + 1U;
  }
  return false;
}

bool cross_selection_match(
    std::span<const Token> tokens,
    const SystemVerilogCoverageDeclaration& cross,
    const SystemVerilogCovergroupSampleResult& sampled,
    const std::span<const SystemVerilogCovergroupSampleInput> inputs) {
  tokens = strip_guard_parentheses(tokens);
  if (tokens.empty()) return false;
  if (const auto operation = guard_operator(tokens, TokenKind::OrOr)) {
    return cross_selection_match(
               tokens.first(*operation), cross, sampled, inputs)
        || cross_selection_match(
               tokens.subspan(*operation + 1U), cross, sampled, inputs);
  }
  if (const auto operation = guard_operator(tokens, TokenKind::AndAnd)) {
    return cross_selection_match(
               tokens.first(*operation), cross, sampled, inputs)
        && cross_selection_match(
               tokens.subspan(*operation + 1U), cross, sampled, inputs);
  }
  if (tokens.front().text == "!") {
    return !cross_selection_match(
        tokens.subspan(1U), cross, sampled, inputs);
  }
  if (tokens.size() < 4U || tokens.front().text != "binsof"
      || tokens[1].kind != TokenKind::LeftParen
      || tokens[2].kind != TokenKind::Identifier) {
    return false;
  }
  std::size_t right{2U};
  int depth{1};
  for (; right < tokens.size(); ++right) {
    if (tokens[right].kind == TokenKind::LeftParen) ++depth;
    if (tokens[right].kind == TokenKind::RightParen) --depth;
    if (depth == 0) break;
  }
  if (right >= tokens.size()) return false;
  const auto operand = std::ranges::find(
      cross.cross_operands,
      tokens[2].text,
      &SystemVerilogCoverageCrossOperand::name);
  if (operand == cross.cross_operands.end()
      || !operand->resolved_declaration_index) {
    return false;
  }
  const auto coverpoint = std::ranges::find(
      sampled.coverpoints,
      *operand->resolved_declaration_index,
      &SystemVerilogCovergroupSampledCoverpoint::coverage_declaration_index);
  if (coverpoint == sampled.coverpoints.end()
      || !coverpoint->result.selected_bin_identity) {
    return false;
  }
  std::optional<std::string_view> bin_name;
  std::size_t suffix = right + 1U;
  if (right >= 5U && tokens[3].kind == TokenKind::Dot
      && tokens[4].kind == TokenKind::Identifier) {
    bin_name = tokens[4].text;
  } else if (suffix + 1U < tokens.size()
             && tokens[suffix].kind == TokenKind::Dot
             && tokens[suffix + 1U].kind == TokenKind::Identifier) {
    bin_name = tokens[suffix + 1U].text;
    suffix += 2U;
  }
  if (bin_name) {
    const auto marker = "." + std::string{*bin_name};
    const auto position = coverpoint->result.selected_bin_identity->rfind(marker);
    if (position == std::string::npos) return false;
    const auto after = position + marker.size();
    if (after != coverpoint->result.selected_bin_identity->size()
        && (*coverpoint->result.selected_bin_identity)[after] != '[') {
      return false;
    }
  }
  if (suffix == tokens.size()) return true;
  if (suffix + 1U >= tokens.size()
      || tokens[suffix].text != "intersect") {
    return false;
  }
  const auto input = std::ranges::find(
      inputs,
      *operand->resolved_declaration_index,
      &SystemVerilogCovergroupSampleInput::coverage_declaration_index);
  return input != inputs.end()
      && cross_intersect_match(tokens.subspan(suffix + 1U), input->value);
}

}  // namespace

SystemVerilogCoverageSampleResult sample_systemverilog_coverpoint(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration,
    const std::size_t coverage_declaration_index,
    const SystemVerilogCoverageSampleValue& value,
    std::vector<Diagnostic>& diagnostics) {
  SystemVerilogCoverageSampleResult result;
  if (instance.declaration_identity != declaration.canonical_identity
      || coverage_declaration_index
          >= declaration.coverage_declarations.size()) {
    return result;
  }
  const auto& coverpoint =
      declaration.coverage_declarations[coverage_declaration_index];
  if (coverpoint.kind
      != SystemVerilogCoverageDeclarationKind::Coverpoint) {
    return result;
  }
  if (!guard_allows(coverpoint.iff_tokens, value)) return result;

  auto previous = std::ranges::find(
      instance.previous_samples,
      coverpoint.declaration_index,
      &SystemVerilogCoveragePreviousSample::coverage_declaration_index);
  const bool had_previous = previous != instance.previous_samples.end();

  const SystemVerilogCoverageBin* regular_match{};
  const SystemVerilogCoverageBin* illegal_match{};
  const SystemVerilogCoverageBin* ignore_match{};
  bool has_transition_bins{};
  for (const auto& bin : coverpoint.bins) {
    if (bin.selection != SystemVerilogCoverageBinSelection::Explicit) {
      continue;
    }
    if (!guard_allows(bin.iff_tokens, value)) continue;
    has_transition_bins = has_transition_bins || !bin.transitions.empty();
    const bool matched = bin.transitions.empty()
        ? exact_match(bin, value)
        : advance_transition_bin(instance, coverpoint, bin, value);
    if (!matched) continue;
    if (bin.kind == SystemVerilogCoverageBinKind::Ignore) {
      if (!ignore_match) ignore_match = &bin;
    } else if (bin.kind == SystemVerilogCoverageBinKind::Illegal) {
      if (!illegal_match) illegal_match = &bin;
    } else if (!regular_match) {
      regular_match = &bin;
    }
  }
  const SystemVerilogCoverageBin* selected = ignore_match
      ? ignore_match : illegal_match ? illegal_match : regular_match;
  if (!selected) {
    const bool active_transition = std::ranges::any_of(
        instance.transition_progress,
        [&](const SystemVerilogCoverageTransitionProgress& progress) {
          return progress.coverage_declaration_index
              == coverpoint.declaration_index;
        });
    const auto fallback_selection = has_transition_bins
        ? SystemVerilogCoverageBinSelection::DefaultSequence
        : SystemVerilogCoverageBinSelection::Default;
    const auto found = std::ranges::find_if(
        coverpoint.bins,
        [&](const SystemVerilogCoverageBin& bin) {
          return bin.selection == fallback_selection
              && guard_allows(bin.iff_tokens, value)
              && (!has_transition_bins
                  || (had_previous && !active_transition));
        });
    if (found != coverpoint.bins.end()) selected = &*found;
  }
  if (!selected && !has_transition_bins && value.unknown_mask == 0U) {
    const auto found = std::ranges::find_if(
        coverpoint.bins,
        [&](const SystemVerilogCoverageBin& bin) {
          return bin.selection
                  == SystemVerilogCoverageBinSelection::Automatic
              && guard_allows(bin.iff_tokens, value);
        });
    if (found != coverpoint.bins.end()) selected = &*found;
  }

  if (!had_previous) {
    instance.previous_samples.push_back(
        {coverpoint.declaration_index, value.value,
         value.unknown_mask, value.width});
  } else {
    previous->value = value.value;
    previous->unknown_mask = value.unknown_mask;
    previous->width = value.width;
  }
  if (!selected) return result;
  std::optional<std::int64_t> automatic_value;
  if (selected->selection == SystemVerilogCoverageBinSelection::Automatic) {
    automatic_value.emplace(value.value);
  }
  result.selected_bin_identity = bin_identity(
      declaration, coverpoint, *selected, automatic_value);
  if (!selected->transitions.empty()) {
    reset_transition_bin(instance, coverpoint, *selected);
  }
  if (selected->weight == 0U) {
    result.zero_weight_excluded = true;
    return result;
  }
  if (selected->kind == SystemVerilogCoverageBinKind::Ignore) {
    result.ignored = true;
    return result;
  }

  if (!record_hit(
      instance,
      declaration,
      coverpoint,
      *selected,
      value.value,
      result,
      diagnostics)) {
    return result;
  }
  if (selected->kind == SystemVerilogCoverageBinKind::Illegal) {
    result.illegal = true;
    const auto identity = result.hit_bin_identities.back();
    instance.illegal_bin_reports.push_back(
        {identity, value.value, selected->span});
    diagnostics.push_back(Diagnostic{
        DiagnosticSeverity::Error,
        "FSIM-SV-COV-001",
        "sampled value " + std::to_string(value.value)
            + " matched illegal coverage bin '" + identity + "'",
        selected->span,
        {}});
  }
  return result;
}

SystemVerilogCoverageSampleResult sample_systemverilog_coverpoint(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration,
    const std::size_t coverage_declaration_index,
    const std::int64_t value,
    std::vector<Diagnostic>& diagnostics) {
  return sample_systemverilog_coverpoint(
      instance,
      declaration,
      coverage_declaration_index,
      SystemVerilogCoverageSampleValue{value, 0U, 64U},
      diagnostics);
}

SystemVerilogCovergroupSampleResult sample_systemverilog_covergroup(
    SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration,
    const std::span<const SystemVerilogCovergroupSampleInput> inputs,
    std::vector<Diagnostic>& diagnostics) {
  SystemVerilogCovergroupSampleResult result;
  if (instance.declaration_identity != declaration.canonical_identity) {
    return result;
  }
  if (!validate_systemverilog_coverage_sample_resources(
          instance, declaration, inputs.size(), diagnostics)) {
    return result;
  }
  std::vector<std::size_t> seen;
  seen.reserve(inputs.size());
  for (const auto& input : inputs) {
    if (input.coverage_declaration_index
            >= declaration.coverage_declarations.size()
        || declaration.coverage_declarations[
               input.coverage_declaration_index].kind
            != SystemVerilogCoverageDeclarationKind::Coverpoint
        || std::ranges::find(seen, input.coverage_declaration_index)
            != seen.end()) {
      return {};
    }
    seen.push_back(input.coverage_declaration_index);
  }

  result.coverpoints.reserve(inputs.size());
  for (const auto& input : inputs) {
    result.coverpoints.push_back({
        input.coverage_declaration_index,
        sample_systemverilog_coverpoint(
            instance,
            declaration,
            input.coverage_declaration_index,
            input.value,
            diagnostics)});
  }

  for (const auto& cross : declaration.coverage_declarations) {
    if (cross.kind != SystemVerilogCoverageDeclarationKind::Cross) continue;
    std::vector<std::string> tuple;
    bool excluded{};
    bool complete{!cross.cross_operands.empty()};
    for (const auto& operand : cross.cross_operands) {
      if (!operand.resolved_declaration_index) {
        complete = false;
        break;
      }
      const auto sampled = std::ranges::find(
          result.coverpoints,
          *operand.resolved_declaration_index,
          &SystemVerilogCovergroupSampledCoverpoint::coverage_declaration_index);
      if (sampled == result.coverpoints.end()
          || !sampled->result.selected_bin_identity) {
        complete = false;
        break;
      }
      tuple.push_back(*sampled->result.selected_bin_identity);
      excluded = excluded || sampled->result.ignored || sampled->result.illegal;
    }
    if (!complete) continue;
    const SystemVerilogCoverageBin* regular_cross_bin{};
    const SystemVerilogCoverageBin* illegal_cross_bin{};
    const SystemVerilogCoverageBin* ignore_cross_bin{};
    for (const auto& bin : cross.bins) {
      if (!cross_selection_match(
              bin.cross_selection_tokens, cross, result, inputs)) {
        continue;
      }
      if (bin.kind == SystemVerilogCoverageBinKind::Ignore) {
        if (!ignore_cross_bin) ignore_cross_bin = &bin;
      } else if (bin.kind == SystemVerilogCoverageBinKind::Illegal) {
        if (!illegal_cross_bin) illegal_cross_bin = &bin;
      } else if (!regular_cross_bin) {
        regular_cross_bin = &bin;
      }
    }
    const auto selected_cross_bin = ignore_cross_bin
        ? ignore_cross_bin
        : illegal_cross_bin ? illegal_cross_bin : regular_cross_bin;
    if (!cross.bins.empty() && !selected_cross_bin) continue;
    const auto weight = selected_cross_bin
        ? selected_cross_bin->weight : cross.effective_weight;
    const auto goal = selected_cross_bin
        ? selected_cross_bin->goal : cross.effective_goal;
    const auto at_least = selected_cross_bin
        ? selected_cross_bin->at_least : cross.effective_at_least;
    excluded = excluded || (selected_cross_bin
        && selected_cross_bin->kind
            != SystemVerilogCoverageBinKind::Regular);
    excluded = excluded || weight == 0U;
    auto identity = declaration.canonical_identity + "::" + cross.name;
    if (selected_cross_bin) identity += "." + selected_cross_bin->name;
    identity += "<";
    for (std::size_t index = 0; index < tuple.size(); ++index) {
      if (index != 0U) identity += ",";
      identity += tuple[index];
    }
    identity += ">";
    auto state = std::ranges::find(
        instance.cross_bin_state,
        identity,
        &SystemVerilogCoverageCrossBinState::identity);
    if (state == instance.cross_bin_state.end()) {
      SystemVerilogCoverageCrossBinState created;
      created.coverage_declaration_index = cross.declaration_index;
      if (selected_cross_bin) {
        created.bin_declaration_index =
            selected_cross_bin->declaration_index;
      }
      created.identity = identity;
      created.operand_bin_identities = tuple;
      created.weight = weight;
      created.goal = goal;
      created.at_least = at_least;
      created.excluded = excluded;
      created.hit_count = excluded ? 0U : 1U;
      created.exclusion_count = excluded ? 1U : 0U;
      created.covered = !excluded && created.hit_count >= created.at_least;
      instance.cross_bin_state.push_back(std::move(created));
    } else if (excluded) {
      state->excluded = true;
      if (state->exclusion_count != std::numeric_limits<std::uint64_t>::max()) {
        ++state->exclusion_count;
      }
    } else {
      if (state->hit_count == std::numeric_limits<std::uint64_t>::max()) {
        diagnostics.push_back(Diagnostic{
            DiagnosticSeverity::Error,
            "FSIM-SV-COV-002",
            "coverage hit count overflow for cross bin '" + identity + "'",
            selected_cross_bin ? selected_cross_bin->span : cross.span,
            {}});
        continue;
      }
      ++state->hit_count;
      state->covered = state->hit_count >= state->at_least;
    }
    if (excluded) {
      result.excluded_cross_bin_identities.push_back(std::move(identity));
    } else {
      result.hit_cross_bin_identities.push_back(std::move(identity));
    }
  }
  return result;
}

}  // namespace fsim::frontend
