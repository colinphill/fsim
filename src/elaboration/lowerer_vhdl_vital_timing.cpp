// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <map>

namespace fsim::elaboration {
using namespace runtime::simir;
using runtime::SimulationTick;

namespace {

std::string_view timing_simple_name(const std::string_view name) {
  const auto separator = name.find_last_of('.');
  return name.substr(
      separator == std::string_view::npos ? 0U : separator + 1U);
}

std::optional<AssertionSeverity> timing_severity(
    const Expression& expression) {
  if (expression.kind != ExpressionKind::Identifier) return std::nullopt;
  if (expression.text == "note") return AssertionSeverity::note;
  if (expression.text == "warning") return AssertionSeverity::warning;
  if (expression.text == "error") return AssertionSeverity::error;
  if (expression.text == "failure") return AssertionSeverity::failure;
  return std::nullopt;
}

std::optional<std::uint16_t> timing_edge(
    const Expression& expression) {
  static constexpr std::array<std::string_view, 16> literals{
      "'/'", "'\\'", "'P'", "'N'", "'r'", "'f'", "'p'", "'n'",
      "'R'", "'F'", "'^'", "'v'", "'E'", "'A'", "'D'", "'*'"};
  const auto found = std::ranges::find(literals, expression.text);
  if (found == literals.end()) return std::nullopt;
  return static_cast<std::uint16_t>(
      std::uint16_t{1U}
      << static_cast<unsigned>(std::distance(literals.begin(), found)));
}

SourceLocation timing_source(const frontend::SourceSpan& span) {
  return SourceLocation{
      span.source_name.str(),
      static_cast<std::uint32_t>(span.begin.line),
      static_cast<std::uint32_t>(span.begin.column)};
}

}  // namespace

bool Lowerer::lower_vhdl_vital_procedure_call(
    const Statement& statement) {
  const auto name = timing_simple_name(statement.procedure_name);
  const bool setup_hold = name == "vitalsetupholdcheck";
  const bool recovery_removal = name == "vitalrecoveryremovalcheck";
  const bool period_pulse = name == "vitalperiodpulsecheck";
  const bool in_phase = name == "vitalinphaseskewcheck";
  const bool out_phase = name == "vitaloutphaseskewcheck";
  if (!setup_hold && !recovery_removal && !period_pulse
      && !in_phase && !out_phase) {
    return false;
  }

  std::vector<const Expression*> positional;
  std::map<std::string, const Expression*, std::less<>> named;
  bool saw_named{};
  bool actuals_valid = true;
  for (const auto& association : statement.procedure_arguments) {
    if (association.formal) {
      saw_named = true;
      if (!named.emplace(*association.formal, &association.value).second) {
        report(
            "FSIM-ELAB-VITAL-010",
            "duplicate VITAL timing actual for formal '"
                + *association.formal + "'",
            association.span);
        actuals_valid = false;
      }
    } else {
      if (saw_named) {
        report(
            "FSIM-ELAB-VITAL-010",
            "a positional VITAL timing actual cannot follow a named actual",
            association.span);
        actuals_valid = false;
      }
      positional.push_back(&association.value);
    }
  }
  const auto actual = [&](const std::string_view formal,
                          const std::size_t position) -> const Expression* {
    const auto selected = named.find(formal);
    if (selected != named.end()) return selected->second;
    return position < positional.size() ? positional[position] : nullptr;
  };
  const auto formals = [&] {
    if (period_pulse) {
      return std::vector<std::string_view>{
          "violation", "perioddata", "testsignal", "testsignalname",
          "testdelay", "period", "pulsewidthhigh", "pulsewidthlow",
          "checkenabled", "headermsg", "xon", "msgon", "msgseverity"};
    }
    if (setup_hold) {
      return std::vector<std::string_view>{
          "violation", "timingdata", "testsignal", "testsignalname",
          "testdelay", "refsignal", "refsignalname", "refdelay",
          "setuphigh", "setuplow", "holdhigh", "holdlow",
          "checkenabled", "reftransition", "headermsg", "xon", "msgon",
          "msgseverity", "enablesetupontest", "enablesetuponref",
          "enableholdonref", "enableholdontest"};
    }
    if (recovery_removal) {
      return std::vector<std::string_view>{
          "violation", "timingdata", "testsignal", "testsignalname",
          "testdelay", "refsignal", "refsignalname", "refdelay",
          "recovery", "removal", "activelow", "checkenabled",
          "reftransition", "headermsg", "xon", "msgon", "msgseverity",
          "enablerecontest", "enablereconref", "enableremonref",
          "enableremontest"};
    }
    return std::vector<std::string_view>{
        "violation", "skewdata", "signal1", "signal1name",
        "signal1delay", "signal2", "signal2name", "signal2delay",
        in_phase ? "skews1s2riserise" : "skews1s2risefall",
        in_phase ? "skews2s1riserise" : "skews2s1risefall",
        in_phase ? "skews1s2fallfall" : "skews1s2fallrise",
        in_phase ? "skews2s1fallfall" : "skews2s1fallrise",
        "checkenabled", "xon", "msgon", "msgseverity", "headermsg",
        "trigger"};
  }();
  if (positional.size() > formals.size()) {
    report(
        "FSIM-ELAB-VITAL-010",
        std::string{name} + " has too many positional actuals",
        statement.span);
    actuals_valid = false;
  }
  for (const auto& [formal, expression] : named) {
    if (std::ranges::find(formals, std::string_view{formal})
        == formals.end()) {
      report(
          "FSIM-ELAB-VITAL-010",
          std::string{name} + " has no formal parameter '" + formal + "'",
          expression->span);
      actuals_valid = false;
    }
  }
  const auto static_time = [&](const std::string_view formal,
                               const std::size_t position,
                               const SimulationTick fallback,
                               bool& valid) {
    const auto* expression = actual(formal, position);
    if (expression == nullptr) return fallback;
    std::string error;
    const auto value = evaluate_constant_expression(*expression, {}, error);
    if (!value || *value < 0) {
      report(
          "FSIM-ELAB-VITAL-011",
          "VITAL timing formal '" + std::string{formal}
              + "' requires a static nonnegative time in project ticks",
          expression->span);
      valid = false;
      return SimulationTick{};
    }
    return static_cast<SimulationTick>(*value);
  };
  const auto static_bool = [&](const std::string_view formal,
                               const std::size_t position,
                               const bool fallback,
                               bool& valid) {
    const auto* expression = actual(formal, position);
    if (expression == nullptr) return fallback;
    std::string error;
    const auto value = evaluate_constant_expression(*expression, {}, error);
    if (!value || (*value != 0 && *value != 1)) {
      report(
          "FSIM-ELAB-VITAL-011",
          "VITAL timing formal '" + std::string{formal}
              + "' requires a static Boolean value",
          expression->span);
      valid = false;
      return fallback;
    }
    return *value != 0;
  };
  const auto static_string = [&](const std::string_view formal,
                                 const std::size_t position,
                                 const std::string_view fallback,
                                 bool& valid) {
    const auto* expression = actual(formal, position);
    if (expression == nullptr) return std::string{fallback};
    if (expression->kind != ExpressionKind::StringLiteral
        || !expression->decoded_string) {
      report(
          "FSIM-ELAB-VITAL-011",
          "VITAL timing formal '" + std::string{formal}
              + "' requires a static string literal",
          expression->span);
      valid = false;
      return std::string{fallback};
    }
    return *expression->decoded_string;
  };
  const auto required_local = [&](const std::string_view formal,
                                  const std::size_t position,
                                  const std::string_view type_fragment,
                                  bool& valid)
      -> std::optional<RegisterId> {
    const auto* expression = actual(formal, position);
    if (expression == nullptr
        || expression->kind != ExpressionKind::Identifier) {
      report(
          "FSIM-ELAB-VITAL-012",
          "VITAL timing formal '" + std::string{formal}
              + "' requires a writable variable",
          expression == nullptr ? statement.span : expression->span);
      valid = false;
      return std::nullopt;
    }
    const auto local = locals_.find(expression->text);
    const auto* type = object_type(expression->text);
    if (local == locals_.end() || type == nullptr
        || (type_fragment.empty()
            ? type->domain != frontend::ValueDomain::Logic9
                || type->width() != 1U
            : timing_simple_name(type->spelling) != type_fragment)) {
      report(
          "FSIM-ELAB-VITAL-012",
          "VITAL timing formal '" + std::string{formal}
              + "' has an incompatible variable type",
          expression->span);
      valid = false;
      return std::nullopt;
    }
    return local->second;
  };
  const auto signal_actual = [&](const std::string_view formal,
                                 const std::size_t position,
                                 const SimulationTick delay,
                                 bool& valid) -> std::optional<SignalId> {
    const auto* expression = actual(formal, position);
    if (expression == nullptr
        || expression->kind != ExpressionKind::Identifier) {
      report(
          "FSIM-ELAB-VITAL-012",
          "VITAL timing formal '" + std::string{formal}
              + "' requires a visible signal",
          expression == nullptr ? statement.span : expression->span);
      valid = false;
      return std::nullopt;
    }
    const auto found = signals_.find(expression->text);
    if (found == signals_.end()) {
      report(
          "FSIM-ELAB-VITAL-012",
          "VITAL timing formal '" + std::string{formal}
              + "' is not a visible signal",
          expression->span);
      valid = false;
      return std::nullopt;
    }
    if (design_.signal_info_[found->second].source_domain
        != frontend::ValueDomain::Logic9) {
      report(
          "FSIM-ELAB-VITAL-012",
          "VITAL timing formal '" + std::string{formal}
              + "' requires a std_ulogic signal or vector",
          expression->span);
      valid = false;
      return std::nullopt;
    }
    if (delay == 0) return found->second;
    const auto delayed = vhdl_implicit_signal_attribute(
        found->second, "delayed", delay, expression->span);
    if (!delayed) valid = false;
    return delayed;
  };

  bool valid = actuals_valid;
  const auto violation = required_local("violation", 0, "", valid);
  const auto state_type = period_pulse
      ? std::string_view{"vitalperioddatatype"}
      : (in_phase || out_phase)
          ? std::string_view{"vitalskewdatatype"}
          : std::string_view{"vitaltimingdatatype"};
  (void)required_local(
      period_pulse ? "perioddata"
                   : (in_phase || out_phase) ? "skewdata" : "timingdata",
      1, state_type, valid);

  VitalTimingCheck operation;
  operation.source = timing_source(statement.span);
  operation.severity = AssertionSeverity::warning;
  std::size_t test_width = 1;
  if (period_pulse) {
    const auto test_delay = static_time("testdelay", 4, 0, valid);
    const auto test = signal_actual("testsignal", 2, test_delay, valid);
    if (test) {
      operation.test_signal = *test;
      test_width = design_.signal_info_[*test].width;
      if (test_width != 1U) {
        report(
            "FSIM-ELAB-VITAL-012",
            "VitalPeriodPulseCheck test signal must be scalar",
            statement.span);
        valid = false;
      }
    }
    operation.kind = VitalTimingCheckKind::period_pulse;
    operation.limits = {
        static_time("period", 5, 0, valid),
        static_time("pulsewidthhigh", 6, 0, valid),
        static_time("pulsewidthlow", 7, 0, valid), 0};
    operation.check_enabled = static_bool("checkenabled", 8, true, valid);
    operation.message = static_string("headermsg", 9, " ", valid)
        + "VitalPeriodPulseCheck("
        + static_string("testsignalname", 3, "", valid) + ")";
    operation.x_on = static_bool("xon", 10, true, valid);
    operation.message_on = static_bool("msgon", 11, true, valid);
    if (const auto* severity = actual("msgseverity", 12)) {
      const auto decoded = timing_severity(*severity);
      if (!decoded) {
        report(
            "FSIM-ELAB-VITAL-011",
            "VITAL MsgSeverity requires a static severity_level literal",
            severity->span);
        valid = false;
      } else {
        operation.severity = *decoded;
      }
    }
  } else if (setup_hold || recovery_removal) {
    const auto test_delay = static_time("testdelay", 4, 0, valid);
    const auto reference_delay = static_time("refdelay", 7, 0, valid);
    const auto test = signal_actual("testsignal", 2, test_delay, valid);
    const auto reference = signal_actual(
        "refsignal", 5, reference_delay, valid);
    if (test) {
      operation.test_signal = *test;
      test_width = design_.signal_info_[*test].width;
    }
    if (reference) operation.reference_signal = *reference;
    if (reference && design_.signal_info_[*reference].width != 1U) {
      report(
          "FSIM-ELAB-VITAL-012",
          "VITAL timing reference signal must be scalar",
          statement.span);
      valid = false;
    }
    if (!setup_hold && test
        && design_.signal_info_[*test].width != 1U) {
      report(
          "FSIM-ELAB-VITAL-012",
          "VitalRecoveryRemovalCheck test signal must be scalar",
          statement.span);
      valid = false;
    }
    operation.kind = setup_hold
        ? VitalTimingCheckKind::setup_hold
        : VitalTimingCheckKind::recovery_removal;
    if (setup_hold) {
      operation.limits = {
          static_time("setuphigh", 8, 0, valid),
          static_time("setuplow", 9, 0, valid),
          static_time("holdhigh", 10, 0, valid),
          static_time("holdlow", 11, 0, valid)};
      operation.check_enabled = static_bool(
          "checkenabled", 12, true, valid);
      if (const auto* edge = actual("reftransition", 13)) {
        const auto decoded = timing_edge(*edge);
        if (decoded) operation.reference_edges = *decoded;
        else {
          report(
              "FSIM-ELAB-VITAL-011",
              "VITAL RefTransition requires a static edge symbol",
              edge->span);
          valid = false;
        }
      } else {
        report(
            "FSIM-ELAB-VITAL-010",
            "VitalSetupHoldCheck requires RefTransition",
            statement.span);
        valid = false;
      }
      operation.message = static_string("headermsg", 14, " ", valid)
          + "VitalSetupHoldCheck("
          + static_string("testsignalname", 3, "", valid) + ","
          + static_string("refsignalname", 6, "", valid) + ")";
      operation.x_on = static_bool("xon", 15, true, valid);
      operation.message_on = static_bool("msgon", 16, true, valid);
      operation.enables = {
          static_bool("enablesetupontest", 18, true, valid),
          static_bool("enablesetuponref", 19, true, valid),
          static_bool("enableholdonref", 20, true, valid),
          static_bool("enableholdontest", 21, true, valid)};
      if (const auto* severity = actual("msgseverity", 17)) {
        const auto decoded = timing_severity(*severity);
        if (decoded) operation.severity = *decoded;
        else {
          report(
              "FSIM-ELAB-VITAL-011",
              "VITAL MsgSeverity requires a static severity_level literal",
              severity->span);
          valid = false;
        }
      }
    } else {
      operation.limits = {
          static_time("recovery", 8, 0, valid),
          static_time("removal", 9, 0, valid), 0, 0};
      operation.active_low = static_bool("activelow", 10, true, valid);
      operation.check_enabled = static_bool(
          "checkenabled", 11, true, valid);
      if (const auto* edge = actual("reftransition", 12)) {
        const auto decoded = timing_edge(*edge);
        if (decoded) operation.reference_edges = *decoded;
        else {
          report(
              "FSIM-ELAB-VITAL-011",
              "VITAL RefTransition requires a static edge symbol",
              edge->span);
          valid = false;
        }
      } else {
        report(
            "FSIM-ELAB-VITAL-010",
            "VitalRecoveryRemovalCheck requires RefTransition",
            statement.span);
        valid = false;
      }
      operation.message = static_string("headermsg", 13, " ", valid)
          + "VitalRecoveryRemovalCheck("
          + static_string("testsignalname", 3, "", valid) + ","
          + static_string("refsignalname", 6, "", valid) + ")";
      operation.x_on = static_bool("xon", 14, true, valid);
      operation.message_on = static_bool("msgon", 15, true, valid);
      operation.enables = {
          static_bool("enablerecontest", 17, true, valid),
          static_bool("enablereconref", 18, true, valid),
          static_bool("enableremonref", 19, true, valid),
          static_bool("enableremontest", 20, true, valid)};
      if (const auto* severity = actual("msgseverity", 16)) {
        const auto decoded = timing_severity(*severity);
        if (decoded) operation.severity = *decoded;
        else {
          report(
              "FSIM-ELAB-VITAL-011",
              "VITAL MsgSeverity requires a static severity_level literal",
              severity->span);
          valid = false;
        }
      }
    }
  } else {
    const auto signal1_delay = static_time("signal1delay", 4, 0, valid);
    const auto signal2_delay = static_time("signal2delay", 7, 0, valid);
    const auto signal1 = signal_actual("signal1", 2, signal1_delay, valid);
    const auto signal2 = signal_actual("signal2", 5, signal2_delay, valid);
    if (signal1) operation.test_signal = *signal1;
    if (signal2) operation.reference_signal = *signal2;
    if ((signal1 && design_.signal_info_[*signal1].width != 1U)
        || (signal2 && design_.signal_info_[*signal2].width != 1U)) {
      report(
          "FSIM-ELAB-VITAL-012",
          "VITAL skew check signals must be scalar",
          statement.span);
      valid = false;
    }
    operation.kind = in_phase
        ? VitalTimingCheckKind::in_phase_skew
        : VitalTimingCheckKind::out_phase_skew;
    operation.limits = {
        static_time(
            in_phase ? "skews1s2riserise" : "skews1s2risefall",
            8, std::numeric_limits<SimulationTick>::max(), valid),
        static_time(
            in_phase ? "skews2s1riserise" : "skews2s1risefall",
            9, std::numeric_limits<SimulationTick>::max(), valid),
        static_time(
            in_phase ? "skews1s2fallfall" : "skews1s2fallrise",
            10, std::numeric_limits<SimulationTick>::max(), valid),
        static_time(
            in_phase ? "skews2s1fallfall" : "skews2s1fallrise",
            11, std::numeric_limits<SimulationTick>::max(), valid)};
    operation.check_enabled = static_bool("checkenabled", 12, true, valid);
    operation.x_on = static_bool("xon", 13, true, valid);
    operation.message_on = static_bool("msgon", 14, true, valid);
    if (const auto* severity = actual("msgseverity", 15)) {
      const auto decoded = timing_severity(*severity);
      if (decoded) operation.severity = *decoded;
      else {
        report(
            "FSIM-ELAB-VITAL-011",
            "VITAL MsgSeverity requires a static severity_level literal",
            severity->span);
        valid = false;
      }
    }
    operation.message = static_string("headermsg", 16, "", valid)
        + (in_phase ? "VitalInPhaseSkewCheck("
                    : "VitalOutPhaseSkewCheck(")
        + static_string("signal1name", 3, "", valid) + ","
        + static_string("signal2name", 6, "", valid) + ")";
    const auto trigger = signal_actual("trigger", 17, 0, valid);
    if (trigger) {
      if (design_.signal_info_[*trigger].width != 1U) {
        report(
            "FSIM-ELAB-VITAL-012",
            "VITAL skew Trigger must be a scalar standard-logic signal",
            statement.span);
        valid = false;
      } else {
        operation.trigger_signal = *trigger;
      }
    }
  }

  if (!valid || !violation) return true;
  if (test_width == 0
      || test_width > std::numeric_limits<std::uint32_t>::max()) {
    report(
        "FSIM-ELAB-VITAL-012",
        "VITAL timing test signal has no bounded executable width",
        statement.span);
    return true;
  }
  std::vector<RegisterId> results;
  results.reserve(test_width);
  for (std::size_t bit = 0; bit < test_width; ++bit) {
    auto instance = operation;
    instance.test_offset = static_cast<std::uint32_t>(bit);
    instance.destination = test_width == 1
        ? *violation
        : allocate_register(1, frontend::ValueDomain::Logic9);
    results.push_back(instance.destination);
    process_.operations.emplace_back(std::move(instance));
  }
  if (results.size() > 1) {
    auto combined = results.front();
    for (std::size_t index = 1; index < results.size(); ++index) {
      const auto next = allocate_register(
          1, frontend::ValueDomain::Logic9);
      process_.operations.emplace_back(Binary{
          BinaryOperator::bit_or, next, combined, results[index]});
      combined = next;
    }
    process_.operations.emplace_back(CopyRegister{*violation, combined});
  }
  return true;
}

}  // namespace fsim::elaboration
