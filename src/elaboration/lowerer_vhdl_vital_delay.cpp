// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <map>

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

std::string_view delay_simple_name(const std::string_view name) {
  const auto separator = name.find_last_of('.');
  return name.substr(
      separator == std::string_view::npos ? 0U : separator + 1U);
}

std::optional<AssertionSeverity> delay_severity(
    const Expression& expression) {
  if (expression.kind != ExpressionKind::Identifier) return std::nullopt;
  if (expression.text == "note") return AssertionSeverity::note;
  if (expression.text == "warning") return AssertionSeverity::warning;
  if (expression.text == "error") return AssertionSeverity::error;
  if (expression.text == "failure") return AssertionSeverity::failure;
  return std::nullopt;
}

SourceLocation delay_source(const frontend::SourceSpan& span) {
  return SourceLocation{
      span.source_name.str(),
      static_cast<std::uint32_t>(span.begin.line),
      static_cast<std::uint32_t>(span.begin.column)};
}

std::optional<VitalGlitchMode> glitch_mode(const Expression& expression) {
  auto name = std::string_view{expression.text};
  constexpr std::string_view prefix{"@fsim-enum:"};
  if (name.starts_with(prefix)) name.remove_prefix(prefix.size());
  if (name == "onevent") return VitalGlitchMode::on_event;
  if (name == "ondetect") return VitalGlitchMode::on_detect;
  if (name == "vitalinertial") return VitalGlitchMode::inertial;
  if (name == "vitaltransport") return VitalGlitchMode::transport;
  return std::nullopt;
}

}  // namespace

bool Lowerer::lower_vhdl_vital_delay_call(const Statement& statement) {
  const auto name = delay_simple_name(statement.procedure_name);
  const bool signal_delay = name == "vitalsignaldelay";
  const bool wire_delay = name == "vitalwiredelay";
  const bool path_single = name == "vitalpathdelay";
  const bool path_01 = name == "vitalpathdelay01";
  const bool path_01z = name == "vitalpathdelay01z";
  const bool path_delay = path_single || path_01 || path_01z;
  if (!signal_delay && !wire_delay && !path_delay) return false;

  std::vector<const Expression*> positional;
  std::map<std::string, const Expression*, std::less<>> named;
  bool saw_named{};
  bool valid = true;
  for (const auto& association : statement.procedure_arguments) {
    if (association.formal) {
      saw_named = true;
      if (!named.emplace(*association.formal, &association.value).second) {
        report(
            "FSIM-ELAB-VITAL-017",
            "duplicate VITAL delay actual for formal '"
                + *association.formal + "'",
            association.span);
        valid = false;
      }
    } else {
      if (saw_named) {
        report(
            "FSIM-ELAB-VITAL-017",
            "a positional VITAL delay actual cannot follow a named actual",
            association.span);
        valid = false;
      }
      positional.push_back(&association.value);
    }
  }
  const auto formals = path_delay
      ? std::vector<std::string_view>{
            "outsignal", "glitchdata", "outsignalname", "outtemp",
            "paths", "defaultdelay", "mode", "xon", "msgon",
            "msgseverity", "outputmap", "negpreempton",
            "ignoredefaultdelay", "rejectfastpath"}
      : signal_delay
          ? std::vector<std::string_view>{"outsig", "insig", "dly"}
          : std::vector<std::string_view>{"outsig", "insig", "twire"};
  const auto maximum_position = path_single ? 12U : path_01 ? 13U : 14U;
  if (positional.size() > (path_delay ? maximum_position : 3U)) {
    report(
        "FSIM-ELAB-VITAL-017",
        std::string{name} + " has too many positional actuals",
        statement.span);
    valid = false;
  }
  for (const auto& [formal, expression] : named) {
    if (std::ranges::find(formals, std::string_view{formal})
        == formals.end()
        || (!path_01z && formal == "outputmap")
        || (path_single && formal == "rejectfastpath")) {
      report(
          "FSIM-ELAB-VITAL-017",
          std::string{name} + " has no formal parameter '" + formal + "'",
          expression->span);
      valid = false;
    }
  }
  const auto actual = [&](const std::string_view formal,
                          const std::size_t position) -> const Expression* {
    const auto selected = named.find(formal);
    if (selected != named.end()) return selected->second;
    return position < positional.size() ? positional[position] : nullptr;
  };
  const auto static_bool = [&](const std::string_view formal,
                               const std::size_t position,
                               const bool fallback) {
    const auto* expression = actual(formal, position);
    if (expression == nullptr) return fallback;
    std::string error;
    const auto value = evaluate_constant_expression(*expression, {}, error);
    if (!value || (*value != 0 && *value != 1)) {
      report(
          "FSIM-ELAB-VITAL-018",
          "VITAL delay formal '" + std::string{formal}
              + "' requires a static Boolean value",
          expression->span);
      valid = false;
      return fallback;
    }
    return *value != 0;
  };
  const auto static_string = [&](const std::string_view formal,
                                 const std::size_t position,
                                 const std::string_view fallback) {
    const auto* expression = actual(formal, position);
    if (expression == nullptr) return std::string{fallback};
    if (expression->kind != ExpressionKind::StringLiteral
        || !expression->decoded_string) {
      report(
          "FSIM-ELAB-VITAL-018",
          "VITAL delay formal '" + std::string{formal}
              + "' requires a static string literal",
          expression->span);
      valid = false;
      return std::string{fallback};
    }
    return *expression->decoded_string;
  };
  const auto signal = [&](const std::string_view formal,
                          const std::size_t position,
                          const bool output) -> std::optional<SignalId> {
    const auto* expression = actual(formal, position);
    if (expression == nullptr
        || expression->kind != ExpressionKind::Identifier) {
      report(
          "FSIM-ELAB-VITAL-019",
          "VITAL delay formal '" + std::string{formal}
              + "' requires a scalar standard-logic signal",
          expression == nullptr ? statement.span : expression->span);
      valid = false;
      return std::nullopt;
    }
    const auto found = signals_.find(expression->text);
    if (found == signals_.end()) {
      report(
          "FSIM-ELAB-VITAL-019",
          "VITAL delay formal '" + std::string{formal}
              + "' is not a visible signal",
          expression->span);
      valid = false;
      return std::nullopt;
    }
    const auto& info = design_.signal_info_[found->second];
    if (info.width != 1U
        || info.source_domain != frontend::ValueDomain::Logic9) {
      report(
          "FSIM-ELAB-VITAL-019",
          "VITAL delay formal '" + std::string{formal}
              + "' requires a scalar standard-logic signal",
          expression->span);
      valid = false;
      return std::nullopt;
    }
    (void)output;
    return found->second;
  };
  const auto zero_register = [&] {
    const auto result = allocate_register(
        64U, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(
        LoadConstant{result, unsigned_value(0U, 64U)});
    return result;
  };
  const auto delay_set = [&](const Expression* expression,
                             const std::size_t count,
                             const std::string_view formal)
      -> std::optional<std::array<RegisterId, 6>> {
    std::array<RegisterId, 6> result{};
    const auto zero = zero_register();
    result.fill(zero);
    if (expression == nullptr) return result;
    const auto contains_negative = [&](const auto& self,
                                       const Expression& value) -> bool {
      if (value.kind == ExpressionKind::Unary && value.text == "-") {
        return true;
      }
      if (value.kind == ExpressionKind::Aggregate) {
        return std::ranges::any_of(
            value.operands,
            [&](const Expression& element) { return self(self, element); });
      }
      std::string error;
      const auto evaluated = evaluate_constant_expression(value, {}, error);
      return evaluated && *evaluated < 0;
    };
    if (contains_negative(contains_negative, *expression)) {
      report(
          "FSIM-ELAB-VITAL-020",
          "VITAL delay formal '" + std::string{formal}
              + "' requires nonnegative delay values",
          expression->span);
      valid = false;
      return std::nullopt;
    }
    const auto type_name = count == 1U
        ? std::string_view{"vitaldelaytype"}
        : count == 2U ? std::string_view{"vitaldelaytype01"}
                      : std::string_view{"vitaldelaytype01z"};
    const auto* type = visible_type_mark(type_name);
    auto value = lower_expression(*expression, count * 64U, type);
    if (!value) {
      report(
          "FSIM-ELAB-VITAL-020",
          "VITAL delay formal '" + std::string{formal}
              + "' requires a compatible nonnegative delay value",
          expression->span);
      valid = false;
      return std::nullopt;
    }
    for (std::size_t index = 0; index < count; ++index) {
      result[index] = allocate_register(
          64U, frontend::ValueDomain::Integer);
      process_.operations.emplace_back(Extract{
          result[index], *value,
          static_cast<std::uint32_t>((count - 1U - index) * 64U), 64U});
    }
    return result;
  };

  VitalDelay operation;
  operation.source_location = delay_source(statement.span);
  operation.severity = AssertionSeverity::warning;
  if (signal_delay || wire_delay) {
    operation.kind = signal_delay
        ? VitalDelayKind::signal : VitalDelayKind::wire;
    const auto output = signal("outsig", 0U, true);
    const auto input = signal("insig", 1U, false);
    const auto* delay_expression = actual(signal_delay ? "dly" : "twire", 2U);
    if (delay_expression == nullptr) {
      report(
          "FSIM-ELAB-VITAL-017",
          std::string{name} + " requires its delay actual",
          statement.span);
      return true;
    }
    std::size_t count = 1U;
    if (wire_delay) {
      const auto width = infer_width(*delay_expression);
      if (width && (*width == 128U || *width == 384U)) {
        count = *width / 64U;
      } else if (delay_expression->kind == ExpressionKind::Aggregate) {
        count = delay_expression->operands.size();
      }
      if (count != 1U && count != 2U && count != 6U) {
        report(
            "FSIM-ELAB-VITAL-020",
            "VitalWireDelay requires scalar, 01, or 01Z delay type",
            delay_expression->span);
        return true;
      }
    }
    operation.shape = count == 1U
        ? VitalDelayShape::single
        : count == 2U ? VitalDelayShape::delay01
                      : VitalDelayShape::delay01z;
    if (operation.shape == VitalDelayShape::delay01z) {
      operation.output_map = allocate_register(
          9U, frontend::ValueDomain::Logic9);
      process_.operations.emplace_back(LoadConstant{
          operation.output_map,
          runtime::PackedLogic4::from_logic9_msb_string("UX01ZWLH-")});
    }
    const auto delays = delay_set(
        delay_expression, count, signal_delay ? "dly" : "twire");
    if (output) operation.output = *output;
    if (input) {
      operation.source = allocate_register(
          1U, frontend::ValueDomain::Logic9);
      process_.operations.emplace_back(
          ReadSignal{operation.source, *input});
    }
    if (delays) operation.default_delays = *delays;
    operation.mode = VitalGlitchMode::transport;
    if (valid && output && input && delays) {
      process_.operations.emplace_back(std::move(operation));
    }
    return true;
  }

  operation.kind = VitalDelayKind::path;
  const auto delay_count = path_single ? 1U : path_01 ? 2U : 6U;
  operation.shape = path_single
      ? VitalDelayShape::single
      : path_01 ? VitalDelayShape::delay01 : VitalDelayShape::delay01z;
  const auto output = signal("outsignal", 0U, true);
  const auto* out_temp = actual("outtemp", 3U);
  if (out_temp == nullptr) {
    report(
        "FSIM-ELAB-VITAL-017",
        std::string{name} + " requires OutTemp",
        statement.span);
    valid = false;
  } else {
    const auto lowered = lower_expression(*out_temp, 1U);
    if (!lowered
        || register_domain(*lowered) != frontend::ValueDomain::Logic9) {
      report(
          "FSIM-ELAB-VITAL-019",
          "VITAL path OutTemp requires a scalar standard-logic value",
          out_temp->span);
      valid = false;
    } else {
      operation.source = *lowered;
    }
  }
  const auto* glitch = actual("glitchdata", 1U);
  if (glitch == nullptr || glitch->kind != ExpressionKind::Identifier) {
    report(
        "FSIM-ELAB-VITAL-019",
        "VITAL path GlitchData requires a writable VitalGlitchDataType variable",
        glitch == nullptr ? statement.span : glitch->span);
    valid = false;
  } else {
    const auto local = locals_.find(glitch->text);
    const auto* type = object_type(glitch->text);
    if (local == locals_.end() || type == nullptr
        || delay_simple_name(type->spelling) != "vitalglitchdatatype") {
      report(
          "FSIM-ELAB-VITAL-019",
          "VITAL path GlitchData has an incompatible variable type",
          glitch->span);
      valid = false;
    } else {
      operation.glitch_data = local->second;
    }
  }
  const auto* paths = actual("paths", 4U);
  if (paths == nullptr || paths->kind != ExpressionKind::Aggregate) {
    report(
        "FSIM-ELAB-VITAL-021",
        "VITAL path Paths requires a static array aggregate",
        paths == nullptr ? statement.span : paths->span);
    valid = false;
  } else {
    if (paths->aggregate_choice_expressions.size()
        != paths->operands.size()) {
      report(
          "FSIM-ELAB-VITAL-021",
          "VITAL path array aggregate metadata is inconsistent",
          paths->span);
      valid = false;
    }
    for (std::size_t row_index = 0;
         row_index < paths->operands.size(); ++row_index) {
      const auto& row = paths->operands[row_index];
      std::size_t copies = 1U;
      if (row_index < paths->aggregate_choice_expressions.size()) {
        const auto& choices = paths->aggregate_choice_expressions[row_index];
        if (!choices.empty()) copies = 0U;
        for (const auto& choice : choices) {
          if (choice.kind == ExpressionKind::Identifier
              && choice.text == "others") {
            report(
                "FSIM-ELAB-VITAL-021",
                "an unconstrained VITAL path aggregate cannot use others",
                choice.span);
            valid = false;
            continue;
          }
          if (choice.kind == ExpressionKind::Binary
              && (choice.text == "to" || choice.text == "downto")
              && choice.operands.size() == 2U) {
            std::string left_error;
            std::string right_error;
            const auto left = evaluate_constant_expression(
                choice.operands[0], {}, left_error);
            const auto right = evaluate_constant_expression(
                choice.operands[1], {}, right_error);
            if (!left || !right) {
              report(
                  "FSIM-ELAB-VITAL-021",
                  "VITAL path range choices require static integer bounds",
                  choice.span);
              valid = false;
              continue;
            }
            const bool descending = choice.text == "downto";
            if ((descending && *left < *right)
                || (!descending && *left > *right)) {
              continue;
            }
            const auto distance = *left >= *right
                ? static_cast<std::uint64_t>(*left)
                    - static_cast<std::uint64_t>(*right)
                : static_cast<std::uint64_t>(*right)
                    - static_cast<std::uint64_t>(*left);
            if (distance >= std::numeric_limits<std::size_t>::max()
                || copies > std::numeric_limits<std::size_t>::max()
                    - static_cast<std::size_t>(distance) - 1U) {
              report(
                  "FSIM-ELAB-VITAL-021",
                  "VITAL path range is not representable by the host",
                  choice.span);
              valid = false;
              continue;
            }
            copies += static_cast<std::size_t>(distance) + 1U;
            continue;
          }
          std::string error;
          if (!evaluate_constant_expression(choice, {}, error)) {
            report(
                "FSIM-ELAB-VITAL-021",
                "VITAL path choices require static integer indices",
                choice.span);
            valid = false;
            continue;
          }
          ++copies;
        }
      }
      if (copies == 0U) continue;
      if (row.kind != ExpressionKind::Aggregate || row.operands.size() != 3U) {
        report(
            "FSIM-ELAB-VITAL-021",
            "each VITAL path record requires InputChangeTime, PathDelay, and "
            "PathCondition",
            row.span);
        valid = false;
        continue;
      }
      VitalPathCandidate candidate;
      const auto input_change = lower_expression(row.operands[0], 64U);
      const auto delays = delay_set(
          &row.operands[1], delay_count, "pathdelay");
      const auto condition = lower_expression(row.operands[2], 1U);
      if (!input_change || !delays || !condition
          || register_domain(*condition) != frontend::ValueDomain::Boolean) {
        report(
            "FSIM-ELAB-VITAL-021",
            "a VITAL path record has an incompatible time, delay, or Boolean field",
            row.span);
        valid = false;
        continue;
      }
      candidate.input_change_time = *input_change;
      candidate.delays = *delays;
      candidate.condition = *condition;
      for (std::size_t copy = 0; copy < copies; ++copy) {
        operation.paths.push_back(candidate);
      }
    }
  }
  const auto defaults = delay_set(
      actual("defaultdelay", 5U), delay_count, "defaultdelay");
  if (defaults) operation.default_delays = *defaults;
  if (const auto* mode = actual("mode", 6U)) {
    const auto decoded = glitch_mode(*mode);
    if (!decoded) {
      report(
          "FSIM-ELAB-VITAL-018",
          "VITAL path Mode requires a static VitalGlitchKindType literal",
          mode->span);
      valid = false;
    } else {
      operation.mode = *decoded;
    }
  }
  operation.x_on = static_bool("xon", 7U, true);
  operation.message_on = static_bool("msgon", 8U, true);
  if (const auto* severity = actual("msgseverity", 9U)) {
    const auto decoded = delay_severity(*severity);
    if (!decoded) {
      report(
          "FSIM-ELAB-VITAL-018",
          "VITAL path MsgSeverity requires a static severity_level literal",
          severity->span);
      valid = false;
    } else {
      operation.severity = *decoded;
    }
  }
  const auto output_name = static_string("outsignalname", 2U, "");
  operation.message = "VitalPathDelay(" + output_name + ")";
  operation.negative_preemption = static_bool(
      "negpreempton", path_01z ? 11U : 10U, false);
  operation.ignore_default_delay = static_bool(
      "ignoredefaultdelay", path_01z ? 12U : 11U, false);
  operation.reject_fast_path = !path_single && static_bool(
      "rejectfastpath", path_01z ? 13U : 12U, false);
  const auto default_map = [&] {
    const auto result = allocate_register(
        9U, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(LoadConstant{
        result, runtime::PackedLogic4::from_logic9_msb_string("UX01ZWLH-")});
    return result;
  }();
  operation.output_map = default_map;
  if (path_01z) {
    if (const auto* map = actual("outputmap", 10U)) {
      const auto lowered = lower_expression(
          *map, 9U, visible_type_mark("vitaloutputmaptype"));
      if (!lowered
          || register_domain(*lowered) != frontend::ValueDomain::Logic9) {
        report(
            "FSIM-ELAB-VITAL-020",
            "VitalPathDelay01Z OutputMap requires VitalOutputMapType",
            map->span);
        valid = false;
      } else {
        operation.output_map = *lowered;
      }
    }
  }
  if (output) operation.output = *output;
  if (valid && output && defaults) {
    process_.operations.emplace_back(std::move(operation));
  }
  return true;
}

}  // namespace fsim::elaboration
