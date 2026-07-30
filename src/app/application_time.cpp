// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app::application_detail {

std::optional<ParsedMagnitude> magnitude_and_unit(std::string_view text)  {
  std::string compact;
  compact.reserve(text.size());
  for (const char character : text) {
    if (std::isspace(static_cast<unsigned char>(character)) == 0
        && character != '_') {
      compact.push_back(static_cast<char>(
          std::tolower(static_cast<unsigned char>(character))));
    }
  }
  const auto split = std::find_if(
      compact.begin(), compact.end(),
      [](const char character) {
        return character < '0' || character > '9';
      });
  if (split == compact.begin()) {
    return std::nullopt;
  }
  const auto* number_end =
      compact.data() + std::distance(compact.begin(), split);
  std::uint64_t magnitude = 0;
  const auto [end, conversion_error] =
      std::from_chars(compact.data(), number_end, magnitude);
  if (conversion_error != std::errc{} || end != number_end) {
    return std::nullopt;
  }
  return ParsedMagnitude{
      magnitude, std::string(split, compact.end())};
}

std::optional<std::uint64_t> unit_femtoseconds(std::string_view unit)  {
  if (unit == "fs") {
    return 1;
  }
  if (unit == "ps") {
    return 1'000;
  }
  if (unit == "ns") {
    return 1'000'000;
  }
  if (unit == "us") {
    return 1'000'000'000;
  }
  if (unit == "ms") {
    return 1'000'000'000'000;
  }
  if (unit == "s") {
    return 1'000'000'000'000'000;
  }
  return std::nullopt;
}

template <typename Function>
void visit_delay(frontend::Delay& delay, Function& function)  {
  function(delay);
  for (auto& additional : delay.additional_values) {
    visit_delay(additional, function);
  }
}

template <typename Function>
void visit_delays(
    std::vector<frontend::Statement>& statements,
    Function&& function)  {
  for (auto& statement : statements) {
    if (!statement.vhdl_waveform.empty()) {
      for (auto& element : statement.vhdl_waveform) {
        if (element.delay) {
          visit_delay(*element.delay, function);
        }
      }
      statement.delay = statement.vhdl_waveform.front().delay;
    } else if (statement.delay) {
      visit_delay(*statement.delay, function);
    }
    if (statement.vhdl_rejection_limit) {
      visit_delay(*statement.vhdl_rejection_limit, function);
    }
    visit_delays(statement.statements, function);
    visit_delays(statement.else_statements, function);
    for (auto& alternative : statement.case_alternatives) {
      visit_delays(alternative.statements, function);
    }
  }
}

void validate_vhdl_rejection_limits(
    const std::vector<frontend::Statement>& statements,
    diagnostic::Engine& diagnostics,
    bool& valid)  {
  for (const auto& statement : statements) {
    if (statement.vhdl_waveform.size() > 1) {
      auto previous =
          statement.vhdl_waveform.front().delay
              ? statement.vhdl_waveform.front().delay->magnitude
              : 0;
      for (std::size_t index = 1;
           index < statement.vhdl_waveform.size();
           ++index) {
        const auto current =
            statement.vhdl_waveform[index].delay
                ? statement.vhdl_waveform[index].delay->magnitude
                : 0;
        if (current <= previous) {
          diagnostics.error(
              "FSIM-VHDL-SEM-034",
              "VHDL waveform-element delays must be strictly ascending",
              span(statement.vhdl_waveform[index].span));
          valid = false;
        }
        previous = current;
      }
    }
    if (statement.vhdl_rejection_limit) {
      const auto mechanism =
          statement.vhdl_delay_mechanism.value_or(
              frontend::VhdlDelayMechanism::ImplicitInertial);
      if (mechanism == frontend::VhdlDelayMechanism::Transport) {
        diagnostics.error(
            "FSIM-VHDL-SEM-033",
            "a VHDL reject clause requires the inertial delay mechanism",
            span(statement.vhdl_rejection_limit->span));
        valid = false;
      }
      const auto first_delay =
          !statement.vhdl_waveform.empty()
              ? (statement.vhdl_waveform.front().delay
                     ? statement.vhdl_waveform.front().delay->magnitude
                     : 0)
              : (statement.delay ? statement.delay->magnitude : 0);
      if (statement.vhdl_rejection_limit->magnitude > first_delay) {
        diagnostics.error(
            "FSIM-VHDL-SEM-032",
            "a VHDL rejection limit cannot exceed the first waveform "
            "element delay",
            span(statement.vhdl_rejection_limit->span));
        valid = false;
      }
    }
    validate_vhdl_rejection_limits(
        statement.statements, diagnostics, valid);
    validate_vhdl_rejection_limits(
        statement.else_statements, diagnostics, valid);
    for (const auto& alternative : statement.case_alternatives) {
      validate_vhdl_rejection_limits(
          alternative.statements, diagnostics, valid);
    }
  }
}

std::string effective_resolution(
    const project::Config& config,
    frontend::ParsedDesign& parsed)  {
  if (config.project.time_resolution != "auto") {
    return config.project.time_resolution;
  }
  std::uint64_t finest_femtoseconds = 1'000'000;
  std::string finest_spelling{"1ns"};
  bool found = false;
  const auto consider_resolution =
      [&](const std::string_view spelling) {
        const auto parsed_time = magnitude_and_unit(spelling);
        if (!parsed_time || parsed_time->unit.empty()) {
          return;
        }
        const auto factor = unit_femtoseconds(parsed_time->unit);
        if (!factor
            || parsed_time->magnitude
                > std::numeric_limits<std::uint64_t>::max() / *factor) {
          return;
        }
        const auto femtoseconds = parsed_time->magnitude * *factor;
        if (!found || femtoseconds < finest_femtoseconds) {
          finest_femtoseconds = femtoseconds;
          finest_spelling =
              std::to_string(parsed_time->magnitude) + parsed_time->unit;
          found = true;
        }
      };
  for (auto& unit : parsed.units) {
    if (!unit.time_precision.empty()) {
      consider_resolution(unit.time_precision);
    }
    auto consider = [&](const frontend::Delay& delay) {
      if (delay.unit.empty()) {
        return;
      }
      consider_resolution("1" + delay.unit);
    };
    visit_delays(unit.concurrent_statements, consider);
    for (auto& process : unit.processes) {
      visit_delays(process.statements, consider);
    }
    for (auto& task : unit.tasks) {
      visit_delays(task.statements, consider);
    }
    for (auto& procedure : unit.procedures) {
      visit_delays(procedure.statements, consider);
    }
  }
  return finest_spelling;
}

bool normalize_delays(
    frontend::ParsedDesign& parsed,
    const std::string_view resolution,
    diagnostic::Engine& diagnostics)  {
  const auto femtoseconds =
      [](const std::string_view spelling)
          -> std::optional<std::uint64_t> {
        const auto parsed_time = magnitude_and_unit(spelling);
        if (!parsed_time || parsed_time->unit.empty()) {
          return std::nullopt;
        }
        const auto factor = unit_femtoseconds(parsed_time->unit);
        if (!factor
            || parsed_time->magnitude
                > std::numeric_limits<std::uint64_t>::max() / *factor) {
          return std::nullopt;
        }
        return parsed_time->magnitude * *factor;
      };
  const auto effective_resolution =
      resolution == "auto" ? std::string_view{"1ns"} : resolution;
  const auto tick_femtoseconds = femtoseconds(effective_resolution);
  bool valid = true;
  for (auto& unit : parsed.units) {
    auto normalize = [&](frontend::Delay& delay) {
      if (delay.unit.empty()) {
        if (delay.divisor != 1) {
          diagnostics.error(
              "FSIM-TIME-0003",
              "a unitless HDL delay cannot retain a fractional tick",
              span(delay.span));
          valid = false;
        }
        return;
      }
      const auto unit_factor = unit_femtoseconds(delay.unit);
      if (!unit_factor || !tick_femtoseconds
          || delay.divisor == 0) {
        diagnostics.error(
            "FSIM-TIME-0003",
            "invalid HDL delay unit, precision, or project resolution",
            span(delay.span));
        valid = false;
        return;
      }
      const bool has_declared_precision =
          !unit.time_precision.empty();
      auto rounding_femtoseconds = *tick_femtoseconds;
      if (has_declared_precision) {
        const auto declared_precision =
            femtoseconds(unit.time_precision);
        if (!declared_precision) {
          diagnostics.error(
              "FSIM-TIME-0003",
              "invalid HDL delay unit, precision, or project resolution",
              span(delay.span));
          valid = false;
          return;
        }
        rounding_femtoseconds = *declared_precision;
      }

      std::array<std::uint64_t, 2> numerator{
          delay.magnitude, *unit_factor};
      std::array<std::uint64_t, 2> denominator{
          delay.divisor, rounding_femtoseconds};
      for (auto& numerator_factor : numerator) {
        for (auto& denominator_factor : denominator) {
          const auto common =
              std::gcd(numerator_factor, denominator_factor);
          numerator_factor /= common;
          denominator_factor /= common;
        }
      }
      if (numerator[0] != 0
          && numerator[1]
              > std::numeric_limits<std::uint64_t>::max()
                  / numerator[0]) {
        diagnostics.error(
            "FSIM-TIME-0003",
            "HDL delay overflows the 64-bit simulation time range",
            span(delay.span));
        valid = false;
        return;
      }
      if (denominator[0] != 0
          && denominator[1]
              > std::numeric_limits<std::uint64_t>::max()
                  / denominator[0]) {
        diagnostics.error(
            "FSIM-TIME-0003",
            "HDL delay precision exceeds the exact decimal range",
            span(delay.span));
        valid = false;
        return;
      }
      const auto numerator_value = numerator[0] * numerator[1];
      const auto denominator_value =
          denominator[0] * denominator[1];
      if (denominator_value == 0) {
        diagnostics.error(
            "FSIM-TIME-0003",
            "HDL delay has a zero normalization denominator",
            span(delay.span));
        valid = false;
        return;
      }
      auto quanta = numerator_value / denominator_value;
      const auto remainder = numerator_value % denominator_value;
      if (has_declared_precision) {
        const auto half =
            denominator_value / 2
            + static_cast<std::uint64_t>(
                denominator_value % 2 != 0);
        if (remainder >= half) {
          if (quanta == std::numeric_limits<std::uint64_t>::max()) {
            diagnostics.error(
                "FSIM-TIME-0003",
                "rounded HDL delay overflows 64-bit simulation time",
                span(delay.span));
            valid = false;
            return;
          }
          ++quanta;
        }
      } else if (remainder != 0) {
        diagnostics.error(
            "FSIM-TIME-0003",
            "time is not exactly representable at resolution '"
                + std::string{effective_resolution} + "'",
            span(delay.span));
        valid = false;
        return;
      }

      std::uint64_t ticks_per_quantum = 1;
      if (has_declared_precision) {
        if (rounding_femtoseconds % *tick_femtoseconds != 0) {
          diagnostics.error(
              "FSIM-TIME-0003",
              "rounded SystemVerilog delay is not representable at "
              "project resolution '" + std::string{effective_resolution}
                  + "'",
              span(delay.span));
          valid = false;
          return;
        }
        ticks_per_quantum =
            rounding_femtoseconds / *tick_femtoseconds;
      }
      if (quanta != 0
          && ticks_per_quantum
              > std::numeric_limits<std::uint64_t>::max() / quanta) {
        diagnostics.error(
            "FSIM-TIME-0003",
            "normalized HDL delay overflows 64-bit simulation ticks",
            span(delay.span));
        valid = false;
        return;
      }
      delay.magnitude = quanta * ticks_per_quantum;
      delay.divisor = 1;
      delay.unit.clear();
    };
    visit_delays(unit.concurrent_statements, normalize);
    for (auto& process : unit.processes) {
      visit_delays(process.statements, normalize);
    }
    for (auto& task : unit.tasks) {
      visit_delays(task.statements, normalize);
    }
    for (auto& procedure : unit.procedures) {
      visit_delays(procedure.statements, normalize);
    }
    validate_vhdl_rejection_limits(
        unit.concurrent_statements, diagnostics, valid);
    for (const auto& process : unit.processes) {
      validate_vhdl_rejection_limits(
          process.statements, diagnostics, valid);
    }
    for (const auto& task : unit.tasks) {
      validate_vhdl_rejection_limits(
          task.statements, diagnostics, valid);
    }
    for (const auto& procedure : unit.procedures) {
      validate_vhdl_rejection_limits(
          procedure.statements, diagnostics, valid);
    }
  }
  return valid;
}

void select_delay_alternatives(
    frontend::ParsedDesign& parsed,
    const project::DelayMode mode)  {
  for (auto& unit : parsed.units) {
    const auto select = [mode](frontend::Delay& delay) {
      const frontend::DelayAlternative* alternative = nullptr;
      switch (mode) {
        case project::DelayMode::minimum:
          alternative =
              delay.minimum ? &*delay.minimum : nullptr;
          break;
        case project::DelayMode::typical:
          alternative =
              delay.typical ? &*delay.typical : nullptr;
          break;
        case project::DelayMode::maximum:
          alternative =
              delay.maximum ? &*delay.maximum : nullptr;
          break;
      }
      if (alternative == nullptr) {
        return;
      }
      delay.magnitude = alternative->magnitude;
      delay.divisor = alternative->divisor;
      delay.unit = alternative->unit;
    };
    visit_delays(unit.concurrent_statements, select);
    for (auto& process : unit.processes) {
      visit_delays(process.statements, select);
    }
    for (auto& task : unit.tasks) {
      visit_delays(task.statements, select);
    }
    for (auto& procedure : unit.procedures) {
      visit_delays(procedure.statements, select);
    }
  }
}

bool validate_declared_time_precisions(
    const frontend::ParsedDesign& parsed,
    const std::string_view resolution,
    diagnostic::Engine& diagnostics)  {
  bool valid = true;
  for (const auto& unit : parsed.units) {
    if (unit.time_precision.empty()) {
      continue;
    }
    std::string error;
    if (!parse_time(unit.time_precision, resolution, error)) {
      diagnostics.error(
          "FSIM-TIME-0004",
          "declared SystemVerilog time precision '"
              + unit.time_precision
              + "' is not representable at project resolution '"
              + std::string{resolution} + "'",
          span(unit.span));
      valid = false;
    }
  }
  return valid;
}

} // namespace fsim::app::application_detail
