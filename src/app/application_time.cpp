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
void visit_clocking_delays(
    std::vector<frontend::SystemVerilogClockingBlock>& blocks,
    Function& function) {
  const auto visit_skew = [&](frontend::SystemVerilogClockingSkew& skew) {
    if (skew.delay) {
      visit_delay(*skew.delay, function);
    }
  };
  for (auto& block : blocks) {
    if (block.default_input_skew) {
      visit_skew(*block.default_input_skew);
    }
    if (block.default_output_skew) {
      visit_skew(*block.default_output_skew);
    }
    for (auto& signal : block.signals) {
      if (signal.skew) {
        visit_skew(*signal.skew);
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
void visit_class_delays(
    std::vector<frontend::SystemVerilogClassDeclaration>& classes,
    Function& function) {
  for (auto& declaration : classes) {
    for (auto& method : declaration.methods) {
      visit_delays(method.statements, function);
    }
    visit_class_delays(declaration.nested_classes, function);
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
    visit_class_delays(body.systemverilog_classes, function);
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

namespace {

[[nodiscard]] diagnostic::SourceSpan compiled_source_span(
    const semantic::Model& model,
    const semantic::SourceSpanId source) {
  const auto& semantic_span = model.source_spans().at(source.value());
  const auto& file = model.source_files().at(semantic_span.file.value());
  diagnostic::SourceSpan result;
  result.path = semantic_span.logical_name.empty()
      ? file.physical_name
      : semantic_span.logical_name;
  result.begin.line = semantic_span.begin.line;
  result.begin.column = semantic_span.begin.column;
  result.begin.offset = semantic_span.begin.offset;
  result.end.line = semantic_span.end.line;
  result.end.column = semantic_span.end.column;
  result.end.offset = semantic_span.end.offset;
  return result;
}

template <typename Delay, typename Function>
void visit_hir_delay(
    const Delay& delay,
    const project::DelayMode mode,
    Function& function) {
  const auto* selected = &delay.primary;
  switch (mode) {
    case project::DelayMode::minimum:
      selected = delay.minimum ? &*delay.minimum : selected;
      break;
    case project::DelayMode::typical:
      selected = delay.typical ? &*delay.typical : selected;
      break;
    case project::DelayMode::maximum:
      selected = delay.maximum ? &*delay.maximum : selected;
      break;
  }
  function(*selected);
  for (const auto& additional : delay.additional) {
    visit_hir_delay(additional, mode, function);
  }
}

template <typename Expression, typename Function>
void visit_physical_time_units(
    const std::vector<const Expression*>& expressions,
    const semantic::ExpressionId id,
    std::set<semantic::ExpressionId>& visited,
    Function& function) {
  if (!visited.insert(id).second) {
    return;
  }
  if (!id.valid() || id.value() >= expressions.size()
      || expressions[id.value()] == nullptr) {
    return;
  }
  const auto& expression = *expressions[id.value()];
  constexpr std::string_view prefix { "@vhdl-physical:" };
  if (expression.text.starts_with(prefix)) {
    const auto unit =
        std::string_view { expression.text }.substr(prefix.size());
    if (unit_femtoseconds(unit)) {
      function("1" + std::string { unit });
    }
  }
  for (const auto operand : expression.operands) {
    visit_physical_time_units(expressions, operand, visited, function);
  }
}

template <typename Expression>
[[nodiscard]] std::vector<const Expression*> index_hir_expressions(
    const std::vector<Expression>& expressions,
    const std::size_t identity_count) {
  std::vector<const Expression*> result(identity_count, nullptr);
  for (const auto& expression : expressions) {
    if (!expression.id.valid()) {
      continue;
    }
    if (expression.id.value() >= result.size()) {
      result.resize(expression.id.value() + 1U, nullptr);
    }
    result[expression.id.value()] = &expression;
  }
  return result;
}

template <typename Function>
void visit_systemverilog_hir_delays(
    const semantic::sv::Hir& hir,
    const project::DelayMode mode,
    Function& function) {
  for (const auto& declaration : hir.declarations()) {
    if (declaration.delay) {
      visit_hir_delay(*declaration.delay, mode, function);
    }
  }
  for (const auto& statement : hir.statements()) {
    if (statement.delay) {
      visit_hir_delay(*statement.delay, mode, function);
    }
  }
  for (const auto& instance : hir.instances()) {
    if (instance.udp_delay) {
      visit_hir_delay(*instance.udp_delay, mode, function);
    }
  }
  for (const auto& unit : hir.units()) {
    for (const auto& timing : unit.timing) {
      for (const auto& specparam : timing.specparams) {
        if (specparam.path_pulse_reject_delay) {
          visit_hir_delay(
              *specparam.path_pulse_reject_delay, mode, function);
        }
        if (specparam.path_pulse_error_delay) {
          visit_hir_delay(
              *specparam.path_pulse_error_delay, mode, function);
        }
      }
      for (const auto& path : timing.module_paths) {
        for (const auto& delay : path.delays) {
          visit_hir_delay(delay, mode, function);
        }
      }
      for (const auto& check : timing.timing_checks) {
        for (const auto& limit : check.normalized_limits) {
          visit_hir_delay(limit, mode, function);
        }
        if (check.normalized_threshold) {
          visit_hir_delay(
              *check.normalized_threshold, mode, function);
        }
      }
    }
    for (const auto& block : unit.clocking_blocks) {
      if (block.default_input_skew
          && block.default_input_skew->delay) {
        visit_hir_delay(
            *block.default_input_skew->delay, mode, function);
      }
      if (block.default_output_skew
          && block.default_output_skew->delay) {
        visit_hir_delay(
            *block.default_output_skew->delay, mode, function);
      }
      for (const auto& signal : block.signals) {
        if (signal.skew && signal.skew->delay) {
          visit_hir_delay(*signal.skew->delay, mode, function);
        }
      }
    }
  }
  // Generated declarations, statements, and instances are owned by the
  // same dense HIR tables and are therefore visited exactly once above.
}

template <typename Function>
void visit_vhdl_generate_disconnection_delays(
    const semantic::vhdl::GenerateRegion& region,
    const project::DelayMode mode,
    Function& function) {
  for (const auto& specification : region.disconnection_specifications) {
    visit_hir_delay(specification.delay, mode, function);
  }
  for (const auto& nested : region.nested) {
    visit_vhdl_generate_disconnection_delays(nested, mode, function);
  }
}

template <typename Function>
void visit_vhdl_hir_delays(
    const semantic::vhdl::Hir& hir,
    const project::DelayMode mode,
    Function& function) {
  for (const auto& statement : hir.statements()) {
    if (statement.delay) {
      visit_hir_delay(*statement.delay, mode, function);
    }
    if (statement.rejection_limit) {
      visit_hir_delay(*statement.rejection_limit, mode, function);
    }
    if (statement.disconnection_delay) {
      visit_hir_delay(*statement.disconnection_delay, mode, function);
    }
    for (const auto& element : statement.waveform) {
      if (element.delay) {
        visit_hir_delay(*element.delay, mode, function);
      }
    }
  }
  for (const auto& unit : hir.units()) {
    for (const auto& specification : unit.disconnection_specifications) {
      visit_hir_delay(specification.delay, mode, function);
    }
    for (const auto& generate : unit.generates) {
      visit_vhdl_generate_disconnection_delays(generate, mode, function);
    }
  }
  // Generate alternatives are represented by nested GenerateRegion records;
  // their statement IDs refer to the dense table visited above.
}

} // namespace

std::string effective_resolution(
    const project::Config& config,
    const semantic::CompiledDesign& compiled)  {
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
  for (const auto& unit : compiled.systemverilog_hir.units()) {
    consider_resolution(unit.compilation.time_unit);
    consider_resolution(unit.compilation.time_precision);
  }
  for (const auto& udp : compiled.systemverilog_hir.udps()) {
    consider_resolution(udp.time_unit);
    consider_resolution(udp.time_precision);
  }
  const auto systemverilog_expression_index = index_hir_expressions(
      compiled.systemverilog_hir.expressions(),
      compiled.semantics.expression_identities().size());
  std::set<semantic::ExpressionId> systemverilog_expressions;
  const auto consider_systemverilog_delay =
      [&](const semantic::sv::DelayValue& delay) {
        if (!delay.unit.empty()) {
          consider_resolution("1" + delay.unit);
        }
        if (delay.expression) {
          visit_physical_time_units(
              systemverilog_expression_index,
              *delay.expression,
              systemverilog_expressions,
              consider_resolution);
        }
      };
  visit_systemverilog_hir_delays(
      compiled.systemverilog_hir,
      config.run.delay_mode,
      consider_systemverilog_delay);
  // A VHDL predefined environment lists every legal physical unit; it is not
  // a declared design precision. Only retained VHDL delay values select the
  // project resolution.
  const auto vhdl_expression_index = index_hir_expressions(
      compiled.vhdl_hir.expressions(),
      compiled.semantics.expression_identities().size());
  std::set<semantic::ExpressionId> vhdl_expressions;
  const auto consider_vhdl_delay =
      [&](const semantic::vhdl::DelayValue& delay) {
        if (!delay.unit.empty()) {
          consider_resolution("1" + delay.unit);
        }
        if (delay.expression) {
          visit_physical_time_units(
              vhdl_expression_index,
              *delay.expression,
              vhdl_expressions,
              consider_resolution);
        }
      };
  visit_vhdl_hir_delays(
      compiled.vhdl_hir, config.run.delay_mode, consider_vhdl_delay);
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
      expression.nominal_type = "@builtin:time";
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
            normalize_expression(statement.loop_update_target);
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
            normalize_statement_expressions(statement.loop_updates);
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
      delay.rounding_quantum = ticks_per_quantum;
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
    visit_clocking_delays(
        unit.systemverilog_clocking_blocks, normalize);
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
    visit_class_delays(unit.systemverilog_classes, normalize);
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
      visit_procedure_delays(procedure, select);
    }
    visit_class_delays(unit.systemverilog_classes, select);
    visit_generate_delays(unit.generate_regions, select);
  }
}

namespace {

template<typename Delay>
void select_hir_delay(Delay& delay, const project::DelayMode mode)
{
    const auto* selected = [&]() -> const decltype(delay.minimum)* {
        switch (mode) {
        case project::DelayMode::minimum:
            return &delay.minimum;
        case project::DelayMode::typical:
            return &delay.typical;
        case project::DelayMode::maximum:
            return &delay.maximum;
        }
        return nullptr;
    }();
    if (selected != nullptr && *selected) {
        delay.primary = **selected;
    }
    delay.minimum.reset();
    delay.typical.reset();
    delay.maximum.reset();
    for (auto& additional : delay.additional) {
        select_hir_delay(additional, mode);
    }
}

template<typename Function>
void visit_systemverilog_hir_delays(
    semantic::sv::Hir& hir,
    Function&& function)
{
    for (auto& declaration : hir.mutable_declarations()) {
        if (declaration.delay) {
            function(*declaration.delay, declaration.scope);
        }
        if (declaration.charge_decay) {
            function(*declaration.charge_decay, declaration.scope);
        }
    }
    for (auto& statement : hir.mutable_statements()) {
        if (statement.delay) {
            function(*statement.delay, statement.scope);
        }
    }
    for (auto& instance : hir.mutable_instances()) {
        if (instance.udp_delay) {
            function(*instance.udp_delay, instance.scope);
        }
    }
    for (auto& unit : hir.mutable_units()) {
        for (auto& timing : unit.timing) {
            for (auto& specparam : timing.specparams) {
                if (specparam.path_pulse_reject_delay) {
                    function(
                        *specparam.path_pulse_reject_delay, unit.scope);
                }
                if (specparam.path_pulse_error_delay) {
                    function(
                        *specparam.path_pulse_error_delay, unit.scope);
                }
            }
            for (auto& path : timing.module_paths) {
                for (auto& delay : path.delays) {
                    function(delay, unit.scope);
                }
            }
            for (auto& check : timing.timing_checks) {
                for (auto& limit : check.normalized_limits) {
                    function(limit, unit.scope);
                }
                if (check.normalized_threshold) {
                    function(*check.normalized_threshold, unit.scope);
                }
            }
        }
        const auto visit_skew = [&](auto& skew) {
            if (skew && skew->delay) {
                function(*skew->delay, unit.scope);
            }
        };
        for (auto& clocking : unit.clocking_blocks) {
            visit_skew(clocking.default_input_skew);
            visit_skew(clocking.default_output_skew);
            for (auto& signal : clocking.signals) {
                visit_skew(signal.skew);
            }
        }
    }
}

template<typename Function>
void visit_vhdl_generate_delays(
    semantic::vhdl::GenerateRegion& generate,
    Function&& function)
{
    for (auto& disconnection : generate.disconnection_specifications) {
        function(disconnection.delay, generate.scope);
    }
    for (auto& nested : generate.nested) {
        visit_vhdl_generate_delays(nested, function);
    }
}

template<typename Function>
void visit_vhdl_hir_delays(
    semantic::vhdl::Hir& hir,
    Function&& function)
{
    for (auto& statement : hir.mutable_statements()) {
        if (statement.delay) {
            function(*statement.delay, statement.scope);
        }
        if (statement.rejection_limit) {
            function(*statement.rejection_limit, statement.scope);
        }
        if (statement.disconnection_delay) {
            function(*statement.disconnection_delay, statement.scope);
        }
        for (auto& waveform : statement.waveform) {
            if (waveform.delay) {
                function(*waveform.delay, statement.scope);
            }
        }
    }
    for (auto& unit : hir.mutable_units()) {
        for (auto& disconnection : unit.disconnection_specifications) {
            function(disconnection.delay, unit.scope);
        }
        for (auto& generate : unit.generates) {
            visit_vhdl_generate_delays(generate, function);
        }
    }
}

[[nodiscard]] std::optional<std::uint64_t> unsigned_literal(
    std::string_view text)
{
    std::string compact;
    compact.reserve(text.size());
    for (const auto character : text) {
        if (character != '_'
            && std::isspace(static_cast<unsigned char>(character)) == 0) {
            compact.push_back(character);
        }
    }
    if (!compact.empty() && compact.front() == '+') {
        compact.erase(compact.begin());
    }
    if (compact.empty()) {
        return std::nullopt;
    }
    std::uint64_t value { };
    const auto [end, error] = std::from_chars(
        compact.data(), compact.data() + compact.size(), value);
    if (error != std::errc { }
        || end != compact.data() + compact.size()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<std::uint64_t> systemverilog_delay_expression(
    const std::vector<const semantic::sv::Expression*>& expressions,
    const semantic::ExpressionId id)
{
    if (!id.valid() || id.value() >= expressions.size()
        || expressions[id.value()] == nullptr) {
        return std::nullopt;
    }
    const auto& expression = *expressions[id.value()];
    if (expression.kind != semantic::sv::ExpressionKind::integer_literal) {
        return std::nullopt;
    }
    return unsigned_literal(expression.text);
}

struct VhdlDelayExpression {
    std::uint64_t magnitude { };
    std::string unit;
};

[[nodiscard]] std::optional<VhdlDelayExpression> vhdl_delay_expression(
    const std::vector<const semantic::vhdl::Expression*>& expressions,
    const semantic::ExpressionId id)
{
    if (!id.valid() || id.value() >= expressions.size()
        || expressions[id.value()] == nullptr) {
        return std::nullopt;
    }
    const auto& expression = *expressions[id.value()];
    if (expression.kind == semantic::vhdl::ExpressionKind::integer_literal) {
        const auto value = unsigned_literal(expression.text);
        return value ? std::optional<VhdlDelayExpression> {
                           { *value, { } } }
                     : std::nullopt;
    }
    constexpr std::string_view prefix { "@vhdl-physical:" };
    if (expression.kind != semantic::vhdl::ExpressionKind::call
        || !expression.text.starts_with(prefix)
        || expression.operands.size() != 1U) {
        return std::nullopt;
    }
    const auto magnitude = vhdl_delay_expression(
        expressions, expression.operands.front());
    if (!magnitude || !magnitude->unit.empty()) {
        return std::nullopt;
    }
    return VhdlDelayExpression {
        magnitude->magnitude,
        expression.text.substr(prefix.size())
    };
}

class HirDelayNormalizer {
public:
    HirDelayNormalizer(
        semantic::CompiledDesign& compiled,
        const std::string_view resolution,
        diagnostic::Engine& diagnostics)
        : compiled_ { compiled }
        , diagnostics_ { diagnostics }
        , systemverilog_expression_index_ { index_hir_expressions(
              compiled.systemverilog_hir.expressions(),
              compiled.semantics.expression_identities().size()) }
        , vhdl_expression_index_ { index_hir_expressions(
              compiled.vhdl_hir.expressions(),
              compiled.semantics.expression_identities().size()) }
        , resolution_ { resolution == "auto"
              ? std::string { "1ns" }
              : std::string { resolution } }
        , tick_femtoseconds_ { femtoseconds(resolution_) }
    {
    }

    [[nodiscard]] bool normalize()
    {
        normalize_vhdl_expressions();
        visit_systemverilog_hir_delays(
            compiled_.systemverilog_hir,
            [&](auto& delay, const semantic::ScopeId scope) {
                normalize_systemverilog(delay, scope);
            });
        visit_vhdl_hir_delays(
            compiled_.vhdl_hir,
            [&](auto& delay, const semantic::ScopeId) {
                normalize_vhdl(delay);
            });
        validate_vhdl_projected_assignments();
        return valid_;
    }

private:
    [[nodiscard]] static std::optional<std::uint64_t> femtoseconds(
        const std::string_view spelling)
    {
        const auto parsed = magnitude_and_unit(spelling);
        if (!parsed || parsed->unit.empty()) {
            return std::nullopt;
        }
        const auto factor = unit_femtoseconds(parsed->unit);
        if (!factor || parsed->magnitude
                > std::numeric_limits<std::uint64_t>::max() / *factor) {
            return std::nullopt;
        }
        return parsed->magnitude * *factor;
    }

    [[nodiscard]] std::string declared_precision(
        const semantic::ScopeId scope) const
    {
        if (!scope.valid()
            || scope.value() >= compiled_.semantics.scopes().size()) {
            return { };
        }
        const auto unit = compiled_.semantics.scopes()[scope.value()].unit;
        if (!unit.valid()) {
            return { };
        }
        const auto found = std::ranges::find(
            compiled_.systemverilog_hir.units(), unit,
            &semantic::sv::Unit::id);
        return found == compiled_.systemverilog_hir.units().end()
            ? std::string { }
            : found->compilation.time_precision;
    }

    void normalize_vhdl_expressions()
    {
        auto& expressions = compiled_.vhdl_hir.mutable_expressions();
        std::vector<semantic::vhdl::Expression*> index(
            compiled_.semantics.expression_identities().size(), nullptr);
        for (auto& expression : expressions) {
            if (!expression.id.valid()) {
                continue;
            }
            if (expression.id.value() >= index.size()) {
                index.resize(expression.id.value() + 1U, nullptr);
            }
            index[expression.id.value()] = &expression;
        }

        // Physical literals occur in constants, qualified expressions, wait
        // conditions, and procedure arguments as well as Delay records.  The
        // compiled-HIR boundary must normalize every such occurrence before
        // elaboration, just as the former syntax-tree pass did.
        std::vector<std::uint8_t> state(index.size());
        const auto visit = [&](const auto& self,
                               const semantic::ExpressionId id) -> void {
            if (!id.valid() || id.value() >= index.size()
                || index[id.value()] == nullptr
                || state[id.value()] == 2U) {
                return;
            }
            if (state[id.value()] == 1U) {
                return;
            }
            state[id.value()] = 1U;
            auto& expression = *index[id.value()];
            const auto operands = expression.operands;
            for (const auto operand : operands) {
                self(self, operand);
            }

            constexpr auto qualification
                = std::string_view { "@vhdl-qualified:time" };
            if (expression.kind == semantic::vhdl::ExpressionKind::call
                && expression.text == qualification
                && expression.operands.size() == 1U) {
                const auto operand = expression.operands.front();
                if (operand.valid() && operand.value() < index.size()
                    && index[operand.value()] != nullptr) {
                    const auto retained_id = expression.id;
                    const auto retained_scope = expression.scope;
                    const auto retained_source = expression.source;
                    const auto retained_origin = expression.origin;
                    const auto retained_dependencies = expression.dependencies;
                    const auto retained_folded = expression.folded;
                    expression = *index[operand.value()];
                    expression.id = retained_id;
                    expression.scope = retained_scope;
                    expression.source = retained_source;
                    expression.origin = retained_origin;
                    expression.dependencies = retained_dependencies;
                    expression.folded = retained_folded;
                    expression.nominal_type = "@builtin:time";
                }
            }

            constexpr auto prefix
                = std::string_view { "@vhdl-physical:" };
            if (expression.kind == semantic::vhdl::ExpressionKind::call
                && expression.text.starts_with(prefix)) {
                normalize_vhdl_physical_expression(expression, index);
            }
            state[id.value()] = 2U;
        };
        for (const auto& expression : expressions) {
            visit(visit, expression.id);
        }
    }

    void normalize_vhdl_physical_expression(
        semantic::vhdl::Expression& expression,
        const std::span<semantic::vhdl::Expression* const> expressions)
    {
        constexpr auto prefix = std::string_view { "@vhdl-physical:" };
        const auto unit = std::string_view { expression.text }.substr(
            prefix.size());
        const auto factor = unit_femtoseconds(unit);
        if (!factor) {
            return;
        }
        if (expression.operands.size() != 1U) {
            vhdl_error(
                "FSIM-ELAB-VHTIME-001",
                "a physical time literal requires a static integer magnitude",
                expression.source);
            return;
        }
        const auto operand = expression.operands.front();
        if (!operand.valid() || operand.value() >= expressions.size()
            || expressions[operand.value()] == nullptr
            || expressions[operand.value()]->kind
                != semantic::vhdl::ExpressionKind::integer_literal) {
            vhdl_error(
                "FSIM-ELAB-VHTIME-001",
                "a physical time literal requires a static integer magnitude",
                expression.source);
            return;
        }
        const auto magnitude = unsigned_literal(
            expressions[operand.value()]->text);
        if (!magnitude || !tick_femtoseconds_ || *tick_femtoseconds_ == 0U) {
            vhdl_error(
                "FSIM-ELAB-VHTIME-002",
                "physical time literal overflows the supported exact range",
                expression.source);
            return;
        }
        const auto common = std::gcd(*factor, *tick_femtoseconds_);
        const auto literal_factor = *factor / common;
        const auto resolution_factor = *tick_femtoseconds_ / common;
        if (*magnitude % resolution_factor != 0U) {
            vhdl_error(
                "FSIM-ELAB-VHTIME-003",
                "physical time literal is not exactly representable at "
                "project resolution '" + resolution_ + "'",
                expression.source);
            return;
        }
        const auto scaled = *magnitude / resolution_factor;
        if (scaled > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()) / literal_factor) {
            vhdl_error(
                "FSIM-ELAB-VHTIME-002",
                "physical time literal overflows the supported exact range",
                expression.source);
            return;
        }

        const auto retained_id = expression.id;
        const auto retained_scope = expression.scope;
        const auto retained_source = expression.source;
        const auto retained_origin = expression.origin;
        semantic::vhdl::Expression replacement;
        replacement.id = retained_id;
        replacement.scope = retained_scope;
        replacement.kind = semantic::vhdl::ExpressionKind::integer_literal;
        replacement.text = std::to_string(scaled * literal_factor);
        replacement.source = retained_source;
        replacement.origin = retained_origin;
        replacement.nominal_type = "@builtin:time";
        replacement.folded = true;
        expression = std::move(replacement);
    }

    template<typename Value>
    void normalize_value(
        Value& value,
        const std::string_view precision,
        const std::optional<std::uint64_t> expression_magnitude,
        const std::string_view expression_unit = { })
    {
        auto magnitude = value.magnitude;
        auto divisor = value.divisor;
        auto unit = value.unit;
        if (expression_magnitude) {
            if (*expression_magnitude != 0U
                && magnitude > std::numeric_limits<std::uint64_t>::max()
                        / *expression_magnitude) {
                error(value.source,
                    "HDL delay overflows the 64-bit simulation time range");
                return;
            }
            magnitude *= *expression_magnitude;
            if (!expression_unit.empty()) {
                if (!unit.empty() && unit != expression_unit) {
                    error(value.source,
                        "HDL delay expression has conflicting time units");
                    return;
                }
                unit = expression_unit;
            }
            value.expression.reset();
        }
        if (unit.empty()) {
            if (divisor != 1U) {
                error(value.source,
                    "a unitless HDL delay cannot retain a fractional tick");
                return;
            }
            value.magnitude = magnitude;
            return;
        }
        const auto unit_factor = unit_femtoseconds(unit);
        if (!unit_factor || !tick_femtoseconds_ || divisor == 0U) {
            error(value.source,
                "invalid HDL delay unit, precision, or project resolution");
            return;
        }
        const bool rounds = !precision.empty();
        auto rounding_femtoseconds = *tick_femtoseconds_;
        if (rounds) {
            const auto declared = femtoseconds(precision);
            if (!declared) {
                error(value.source,
                    "invalid HDL delay unit, precision, or project resolution");
                return;
            }
            rounding_femtoseconds = *declared;
        }
        std::array<std::uint64_t, 2> numerator {
            magnitude, *unit_factor
        };
        std::array<std::uint64_t, 2> denominator {
            divisor, rounding_femtoseconds
        };
        for (auto& numerator_factor : numerator) {
            for (auto& denominator_factor : denominator) {
                const auto common = std::gcd(
                    numerator_factor, denominator_factor);
                numerator_factor /= common;
                denominator_factor /= common;
            }
        }
        if ((numerator[0] != 0U
                && numerator[1]
                    > std::numeric_limits<std::uint64_t>::max()
                        / numerator[0])
            || (denominator[0] != 0U
                && denominator[1]
                    > std::numeric_limits<std::uint64_t>::max()
                        / denominator[0])) {
            error(value.source,
                "HDL delay exceeds the supported exact decimal range");
            return;
        }
        const auto numerator_value = numerator[0] * numerator[1];
        const auto denominator_value = denominator[0] * denominator[1];
        if (denominator_value == 0U) {
            error(value.source,
                "HDL delay has a zero normalization denominator");
            return;
        }
        auto quanta = numerator_value / denominator_value;
        const auto remainder = numerator_value % denominator_value;
        if (rounds) {
            const auto half = denominator_value / 2U
                + static_cast<std::uint64_t>(
                    denominator_value % 2U != 0U);
            if (remainder >= half) {
                if (quanta == std::numeric_limits<std::uint64_t>::max()) {
                    error(value.source,
                        "rounded HDL delay overflows 64-bit simulation time");
                    return;
                }
                ++quanta;
            }
        } else if (remainder != 0U) {
            error(value.source,
                "time is not exactly representable at resolution '"
                    + resolution_ + "'");
            return;
        }
        std::uint64_t ticks_per_quantum = 1U;
        if (rounds) {
            if (rounding_femtoseconds % *tick_femtoseconds_ != 0U) {
                error(value.source,
                    "rounded SystemVerilog delay is not representable at "
                    "project resolution '" + resolution_ + "'");
                return;
            }
            ticks_per_quantum
                = rounding_femtoseconds / *tick_femtoseconds_;
        }
        if (quanta != 0U
            && ticks_per_quantum
                > std::numeric_limits<std::uint64_t>::max() / quanta) {
            error(value.source,
                "normalized HDL delay overflows 64-bit simulation ticks");
            return;
        }
        value.magnitude = quanta * ticks_per_quantum;
        value.divisor = 1U;
        value.unit.clear();
    }

    void normalize_systemverilog(
        semantic::sv::Delay& delay,
        const semantic::ScopeId scope)
    {
        const auto precision = declared_precision(scope);
        const auto normalize = [&](auto& value) {
            const auto expression = value.expression
                ? systemverilog_delay_expression(
                    systemverilog_expression_index_, *value.expression)
                : std::nullopt;
            normalize_value(value, precision, expression);
        };
        normalize(delay.primary);
        if (delay.minimum) {
            normalize(*delay.minimum);
        }
        if (delay.typical) {
            normalize(*delay.typical);
        }
        if (delay.maximum) {
            normalize(*delay.maximum);
        }
        for (auto& additional : delay.additional) {
            normalize_systemverilog(additional, scope);
        }
    }

    void normalize_vhdl(semantic::vhdl::Delay& delay)
    {
        const auto normalize = [&](auto& value) {
            const auto expression = value.expression
                ? vhdl_delay_expression(
                    vhdl_expression_index_, *value.expression)
                : std::nullopt;
            normalize_value(
                value,
                { },
                expression
                    ? std::optional<std::uint64_t> {
                        expression->magnitude }
                    : std::nullopt,
                expression ? expression->unit : std::string_view { });
        };
        normalize(delay.primary);
        if (delay.minimum) {
            normalize(*delay.minimum);
        }
        if (delay.typical) {
            normalize(*delay.typical);
        }
        if (delay.maximum) {
            normalize(*delay.maximum);
        }
        for (auto& additional : delay.additional) {
            normalize_vhdl(additional);
        }
    }

    void validate_vhdl_projected_assignments()
    {
        for (const auto& statement : compiled_.vhdl_hir.statements()) {
            if (statement.kind
                    != semantic::vhdl::StatementKind::signal_assignment) {
                continue;
            }
            const auto delay_magnitude = [](const auto& delay) {
                return delay ? delay->primary.magnitude : 0U;
            };
            if (statement.waveform.size() > 1U) {
                auto previous = delay_magnitude(
                    statement.waveform.front().delay);
                for (std::size_t index { 1U };
                    index < statement.waveform.size(); ++index) {
                    const auto current = delay_magnitude(
                        statement.waveform[index].delay);
                    if (current <= previous) {
                        vhdl_error(
                            "FSIM-VHDL-SEM-034",
                            "VHDL waveform-element delays must be strictly "
                            "ascending",
                            statement.waveform[index].source);
                    }
                    previous = current;
                }
            }
            if (!statement.rejection_limit) {
                continue;
            }
            if (statement.delay_mechanism
                    == semantic::vhdl::DelayMechanism::transport) {
                vhdl_error(
                    "FSIM-VHDL-SEM-033",
                    "a VHDL reject clause requires the inertial delay "
                    "mechanism",
                    statement.rejection_limit->primary.source);
            }
            const auto first_delay = !statement.waveform.empty()
                ? delay_magnitude(statement.waveform.front().delay)
                : delay_magnitude(statement.delay);
            if (statement.rejection_limit->primary.magnitude
                > first_delay) {
                vhdl_error(
                    "FSIM-VHDL-SEM-032",
                    "a VHDL rejection limit cannot exceed the first "
                    "waveform element delay",
                    statement.rejection_limit->primary.source);
            }
        }
    }

    void vhdl_error(
        const std::string_view code,
        std::string message,
        const semantic::SourceSpanId source)
    {
        diagnostics_.error(
            std::string { code },
            std::move(message),
            compiled_source_span(compiled_.semantics, source));
        valid_ = false;
    }

    void error(
        const semantic::SourceSpanId source,
        std::string message)
    {
        diagnostics_.error(
            "FSIM-TIME-0003",
            std::move(message),
            compiled_source_span(compiled_.semantics, source));
        valid_ = false;
    }

    semantic::CompiledDesign& compiled_;
    diagnostic::Engine& diagnostics_;
    std::vector<const semantic::sv::Expression*>
        systemverilog_expression_index_;
    std::vector<const semantic::vhdl::Expression*> vhdl_expression_index_;
    std::string resolution_;
    std::optional<std::uint64_t> tick_femtoseconds_;
    bool valid_ { true };
};

} // namespace

void select_delay_alternatives(
    semantic::CompiledDesign& compiled,
    const project::DelayMode mode)
{
    visit_systemverilog_hir_delays(
        compiled.systemverilog_hir,
        [&](auto& delay, const semantic::ScopeId) {
            select_hir_delay(delay, mode);
        });
    visit_vhdl_hir_delays(
        compiled.vhdl_hir,
        [&](auto& delay, const semantic::ScopeId) {
            select_hir_delay(delay, mode);
        });
}

bool normalize_delays(
    semantic::CompiledDesign& compiled,
    const std::string_view resolution,
    diagnostic::Engine& diagnostics)
{
    return HirDelayNormalizer {
        compiled, resolution, diagnostics
    }.normalize();
}

bool validate_declared_time_precisions(
    const semantic::CompiledDesign& compiled,
    const std::string_view resolution,
    diagnostic::Engine& diagnostics)  {
  bool valid = true;
  const auto validate = [&](const std::string& precision,
                            const semantic::SourceSpanId source) {
    if (precision.empty()) {
      return;
    }
    std::string error;
    if (!parse_time(precision, resolution, error)) {
      diagnostics.error(
          "FSIM-TIME-0004",
          "declared SystemVerilog time precision '"
              + precision
              + "' is not representable at project resolution '"
              + std::string { resolution } + "'",
          compiled_source_span(compiled.semantics, source));
      valid = false;
    }
  };
  for (const auto& unit : compiled.systemverilog_hir.units()) {
    validate(unit.compilation.time_precision, unit.source);
  }
  for (const auto& udp : compiled.systemverilog_hir.udps()) {
    validate(udp.time_precision, udp.source);
  }
  return valid;
}

} // namespace fsim::app::application_detail
