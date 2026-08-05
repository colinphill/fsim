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
  if (unit == "s" || unit == "sec") {
    return 1'000'000'000'000'000;
  }
  if (unit == "min") {
    return 60'000'000'000'000'000;
  }
  if (unit == "hr") {
    return 3'600'000'000'000'000'000;
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

template <typename Function>
void visit_signal_delays(
    std::vector<frontend::SignalDeclaration>& signals,
    Function& function) {
  for (auto& signal : signals) {
    if (signal.net_delay) {
      visit_delay(*signal.net_delay, function);
    }
    if (signal.charge_decay) {
      visit_delay(*signal.charge_decay, function);
    }
  }
}

template <typename Function>
void visit_instance_delays(
    std::vector<frontend::Instance>& instances,
    Function& function) {
  for (auto& instance : instances) {
    if (instance.udp_delay) {
      visit_delay(*instance.udp_delay, function);
    }
  }
}

template <typename Function>
void visit_specify_delays(
    std::vector<frontend::VerilogSpecifyBlock>& blocks,
    Function& function) {
  for (auto& block : blocks) {
    for (auto& specparam : block.specparams) {
      if (specparam.path_pulse_reject_delay) {
        visit_delay(*specparam.path_pulse_reject_delay, function);
      }
      if (specparam.path_pulse_error_delay) {
        visit_delay(*specparam.path_pulse_error_delay, function);
      }
    }
    for (auto& path : block.module_paths) {
      for (auto& delay : path.delays) {
        visit_delay(delay, function);
      }
    }
    for (auto& check : block.timing_checks) {
      for (auto& limit : check.normalized_limits) {
        visit_delay(limit, function);
      }
      if (check.normalized_threshold) {
        visit_delay(*check.normalized_threshold, function);
      }
    }
  }
}

template <typename Function>
void visit_procedure_delays(
    frontend::ProcedureDeclaration& procedure,
    Function& function) {
  visit_delays(procedure.statements, function);
  for (auto& nested : procedure.procedures) {
    visit_procedure_delays(nested, function);
  }
}

template <typename Function>
void visit_generate_delays(
    std::vector<frontend::GenerateRegion>& regions,
    Function& function) {
  const auto visit_body = [&](frontend::GenerateBody& body) {
    visit_signal_delays(body.signals, function);
    visit_instance_delays(body.instances, function);
    visit_delays(body.concurrent_statements, function);
    for (auto& process : body.processes) {
      visit_delays(process.statements, function);
      for (auto& procedure : process.procedures) {
        visit_procedure_delays(procedure, function);
      }
    }
    for (auto& task : body.tasks) {
      visit_delays(task.statements, function);
    }
    for (auto& procedure : body.procedures) {
      visit_procedure_delays(procedure, function);
    }
    visit_generate_delays(body.generate_regions, function);
  };
  for (auto& region : regions) {
    visit_body(region.then_body);
    visit_body(region.else_body);
    for (auto& alternative : region.alternatives) {
      visit_body(alternative.body);
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
      if (!delay.unit.empty()) {
        consider_resolution("1" + delay.unit);
      }
      if (!delay.expression) {
        return;
      }
      const auto visit_expression =
          [&](const auto& self,
              const frontend::Expression& expression) -> void {
        constexpr std::string_view prefix{"@vhdl-physical:"};
        if (expression.kind == frontend::ExpressionKind::Call
            && expression.text.starts_with(prefix)
            && unit_femtoseconds(expression.text.substr(prefix.size()))) {
          consider_resolution(
              "1" + expression.text.substr(prefix.size()));
        }
        for (const auto& operand : expression.operands) {
          self(self, operand);
        }
      };
      visit_expression(visit_expression, *delay.expression);
    };
    visit_signal_delays(unit.ports, consider);
    visit_signal_delays(unit.signals, consider);
    visit_instance_delays(unit.instances, consider);
    visit_delays(unit.concurrent_statements, consider);
    for (auto& process : unit.processes) {
      visit_delays(process.statements, consider);
      for (auto& procedure : process.procedures) {
        visit_procedure_delays(procedure, consider);
      }
    }
    for (auto& task : unit.tasks) {
      visit_delays(task.statements, consider);
    }
    for (auto& procedure : unit.procedures) {
      visit_procedure_delays(procedure, consider);
    }
    visit_generate_delays(unit.generate_regions, consider);
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
    std::function<bool(frontend::Expression&)> normalize_time_literals;
    normalize_time_literals = [&](frontend::Expression& expression) {
      for (auto& operand : expression.operands) {
        if (!normalize_time_literals(operand)) return false;
      }
      constexpr std::string_view qualification{"@vhdl-qualified:time"};
      if (expression.kind == frontend::ExpressionKind::Call
          && expression.text == qualification
          && expression.operands.size() == 1) {
        const auto retained_span = expression.span;
        auto replacement = std::move(expression.operands.front());
        expression = std::move(replacement);
        expression.span = retained_span;
        return true;
      }
      constexpr std::string_view prefix{"@vhdl-physical:"};
      if (expression.kind != frontend::ExpressionKind::Call
          || !expression.text.starts_with(prefix)) {
        return true;
      }
      const auto unit_name = expression.text.substr(prefix.size());
      const auto factor = unit_femtoseconds(unit_name);
      if (!factor) {
        return true;
      }
      if (expression.operands.size() != 1
          || expression.operands.front().kind
              != frontend::ExpressionKind::IntegerLiteral) {
        diagnostics.error(
            "FSIM-ELAB-VHTIME-001",
            "a physical time literal requires a static integer magnitude",
            span(expression.span));
        valid = false;
        return false;
      }
      const auto parsed_literal = magnitude_and_unit(
          expression.operands.front().text + unit_name);
      if (!parsed_literal || !tick_femtoseconds) {
        diagnostics.error(
            "FSIM-ELAB-VHTIME-002",
            "physical time literal overflows the supported exact range",
            span(expression.span));
        valid = false;
        return false;
      }
      if (*tick_femtoseconds == 0) {
        diagnostics.error(
            "FSIM-ELAB-VHTIME-002",
            "physical time literal overflows the supported exact range",
            span(expression.span));
        valid = false;
        return false;
      }
      const auto common = std::gcd(*factor, *tick_femtoseconds);
      const auto literal_factor = *factor / common;
      const auto resolution_factor = *tick_femtoseconds / common;
      if (parsed_literal->magnitude % resolution_factor != 0) {
        diagnostics.error(
            "FSIM-ELAB-VHTIME-003",
            "physical time literal is not exactly representable at project "
            "resolution '" + std::string{effective_resolution} + "'",
            span(expression.span));
        valid = false;
        return false;
      }
      const auto scaled_magnitude =
          parsed_literal->magnitude / resolution_factor;
      if (scaled_magnitude
          > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())
              / literal_factor) {
        diagnostics.error(
            "FSIM-ELAB-VHTIME-002",
            "physical time literal overflows the supported exact range",
            span(expression.span));
        valid = false;
        return false;
      }
      expression.kind = frontend::ExpressionKind::IntegerLiteral;
      expression.text = std::to_string(
          scaled_magnitude * literal_factor);
      expression.operands.clear();
      return true;
    };
    const auto normalize_expression = [&](frontend::Expression& expression) {
      if (expression.valid()) {
        (void)normalize_time_literals(expression);
      }
    };
    std::function<void(std::vector<frontend::Statement>&)>
        normalize_statement_expressions;
    normalize_statement_expressions =
        [&](std::vector<frontend::Statement>& statements) {
          for (auto& statement : statements) {
            normalize_expression(statement.target);
            normalize_expression(statement.value);
            normalize_expression(statement.condition);
            normalize_expression(statement.loop_initial);
            normalize_expression(statement.loop_limit);
            normalize_expression(statement.vhdl_guard);
            normalize_expression(statement.vhdl_report_expression);
            normalize_expression(statement.vhdl_severity_expression);
            normalize_expression(statement.file_handle);
            for (auto& argument : statement.task_arguments) {
              normalize_expression(argument);
            }
            for (auto& association : statement.procedure_arguments) {
              normalize_expression(association.value);
            }
            for (auto& waveform : statement.vhdl_waveform) {
              normalize_expression(waveform.value);
            }
            for (auto& sensitivity : statement.sensitivities) {
              normalize_expression(sensitivity.expression);
            }
            for (auto& output : statement.output_values) {
              normalize_expression(output.value);
            }
            for (auto& declaration : statement.declarations) {
              if (declaration.initializer) {
                normalize_expression(*declaration.initializer);
              }
            }
            normalize_statement_expressions(statement.statements);
            normalize_statement_expressions(statement.else_statements);
            for (auto& alternative : statement.case_alternatives) {
              for (auto& choice : alternative.choices) {
                normalize_expression(choice);
              }
              normalize_statement_expressions(alternative.statements);
            }
          }
        };
    const auto normalize_constants = [&](auto& constants) {
      for (auto& constant : constants) {
        if (constant.default_value.valid()) {
          (void)normalize_time_literals(constant.default_value);
        }
      }
    };
    if (unit.language == frontend::Language::Vhdl2008) {
      normalize_constants(unit.parameters);
      std::function<void(frontend::FunctionDeclaration&)>
          normalize_function;
      std::function<void(frontend::ProcedureDeclaration&)>
          normalize_procedure;
      normalize_function = [&](frontend::FunctionDeclaration& function) {
        normalize_constants(function.constants);
        normalize_statement_expressions(function.statements);
        for (auto& argument : function.arguments) {
          if (argument.default_value) {
            (void)normalize_time_literals(*argument.default_value);
          }
        }
        for (auto& nested : function.functions) {
          normalize_function(nested);
        }
        for (auto& nested : function.procedures) {
          normalize_procedure(nested);
        }
      };
      normalize_procedure =
          [&](frontend::ProcedureDeclaration& procedure) {
        normalize_constants(procedure.constants);
        normalize_statement_expressions(procedure.statements);
        for (auto& argument : procedure.arguments) {
          if (argument.default_value) {
            (void)normalize_time_literals(*argument.default_value);
          }
        }
        for (auto& nested : procedure.functions) {
          normalize_function(nested);
        }
        for (auto& nested : procedure.procedures) {
          normalize_procedure(nested);
        }
      };
      for (auto& process : unit.processes) {
        normalize_constants(process.constants);
        normalize_statement_expressions(process.statements);
        for (auto& sensitivity : process.sensitivities) {
          normalize_expression(sensitivity.expression);
        }
        for (auto& function : process.functions) {
          normalize_function(function);
        }
        for (auto& procedure : process.procedures) {
          normalize_procedure(procedure);
        }
      }
      for (auto& function : unit.functions) {
        normalize_function(function);
      }
      for (auto& procedure : unit.procedures) {
        normalize_procedure(procedure);
      }
      for (auto& function_template : unit.generic_function_templates) {
        normalize_function(function_template.function);
      }
      for (auto& procedure_template : unit.generic_procedure_templates) {
        normalize_procedure(procedure_template.procedure);
      }
      normalize_statement_expressions(unit.concurrent_statements);
      const auto normalize_generate_body =
          [&](const auto& self, frontend::GenerateBody& body) -> void {
        normalize_constants(body.constants);
        for (auto& process : body.processes) {
          normalize_constants(process.constants);
          normalize_statement_expressions(process.statements);
          for (auto& sensitivity : process.sensitivities) {
            normalize_expression(sensitivity.expression);
          }
          for (auto& function : process.functions) {
            normalize_function(function);
          }
          for (auto& procedure : process.procedures) {
            normalize_procedure(procedure);
          }
        }
        for (auto& function : body.functions) {
          normalize_function(function);
        }
        for (auto& procedure : body.procedures) {
          normalize_procedure(procedure);
        }
        for (auto& function_template : body.generic_function_templates) {
          normalize_function(function_template.function);
        }
        for (auto& procedure_template : body.generic_procedure_templates) {
          normalize_procedure(procedure_template.procedure);
        }
        normalize_statement_expressions(body.concurrent_statements);
        for (auto& region : body.generate_regions) {
          self(self, region.then_body);
          self(self, region.else_body);
          for (auto& alternative : region.alternatives) {
            self(self, alternative.body);
          }
        }
      };
      for (auto& region : unit.generate_regions) {
        normalize_generate_body(normalize_generate_body, region.then_body);
        normalize_generate_body(normalize_generate_body, region.else_body);
        for (auto& alternative : region.alternatives) {
          normalize_generate_body(
              normalize_generate_body, alternative.body);
        }
      }
    }
    auto normalize = [&](frontend::Delay& delay) {
      if (unit.language == frontend::Language::Vhdl2008
          && delay.expression) {
        if (normalize_time_literals(*delay.expression)) {
          delay.magnitude = 1;
        }
        return;
      }
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
    visit_signal_delays(unit.ports, normalize);
    visit_signal_delays(unit.signals, normalize);
    visit_instance_delays(unit.instances, normalize);
    visit_specify_delays(unit.verilog_specify_blocks, normalize);
    visit_delays(unit.concurrent_statements, normalize);
    for (auto& process : unit.processes) {
      visit_delays(process.statements, normalize);
      for (auto& procedure : process.procedures) {
        visit_procedure_delays(procedure, normalize);
      }
    }
    for (auto& task : unit.tasks) {
      visit_delays(task.statements, normalize);
    }
    for (auto& procedure : unit.procedures) {
      visit_procedure_delays(procedure, normalize);
    }
    visit_generate_delays(unit.generate_regions, normalize);
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
      delay.expression = alternative->expression;
    };
    visit_signal_delays(unit.ports, select);
    visit_signal_delays(unit.signals, select);
    visit_instance_delays(unit.instances, select);
    visit_specify_delays(unit.verilog_specify_blocks, select);
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
    visit_generate_delays(unit.generate_regions, select);
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
