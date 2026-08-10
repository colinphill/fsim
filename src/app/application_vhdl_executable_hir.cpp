// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app::application_detail {
namespace {

namespace vh = semantic::vhdl;

[[nodiscard]] std::string canonical_vhdl_name(std::string value) {
  if (!value.empty() && value.front() != '\\' && value.front() != '\'') {
    std::transform(value.begin(), value.end(), value.begin(), [](const char c) {
      return static_cast<char>(
          std::tolower(static_cast<unsigned char>(c)));
    });
  }
  return value;
}

class VhdlExecutableBuilder final {
 public:
  VhdlExecutableBuilder(semantic::Model& model, vh::Hir& hir)
      : model_(model), hir_(hir) {}

  void add_design(const frontend::ParsedDesign& parsed) {
    for (std::size_t index = 0; index < parsed.units.size(); ++index) {
      const auto& input = parsed.units[index];
      if (input.language != frontend::Language::Vhdl2008) {
        continue;
      }
      const auto unit_id = semantic::UnitId::from_index(
          static_cast<std::uint32_t>(index));
      const auto found = std::ranges::find_if(
          hir_.mutable_units(), [&](const vh::Unit& unit) {
            return unit.id == unit_id;
          });
      if (found == hir_.mutable_units().end()) {
        throw std::logic_error{"missing VHDL HIR unit"};
      }
      fill_unit(input, *found);
    }
    rebuild_overload_sets();
    resolve_expression_names();
  }

 private:
  [[nodiscard]] semantic::SourceSpanId source(
      const frontend::SourceSpan& span) {
    return intern_semantic_span(model_, span);
  }

  [[nodiscard]] semantic::OriginId origin(
      const semantic::SourceSpanId span,
      const semantic::OriginId parent,
      const std::string_view detail) {
    return model_.add_origin(
        semantic::OriginKind::parsed, span, parent, std::string{detail});
  }

  [[nodiscard]] std::optional<semantic::ScopeId> parent_scope(
      const semantic::ScopeId scope) const noexcept {
    return model_.scopes()[scope.value()].parent;
  }

  [[nodiscard]] vh::Name name(
      const std::string_view spelling,
      const frontend::SourceSpan& span,
      const semantic::ScopeId scope) {
    vh::Name result{
        std::string{spelling}, canonical_vhdl_name(std::string{spelling}),
        source(span), std::nullopt, {}};
    auto visible_scope = std::optional<semantic::ScopeId>{scope};
    while (visible_scope) {
      const auto found = std::ranges::find_if(
          hir_.overload_sets(), [&](const vh::OverloadSet& overload) {
            return overload.scope == *visible_scope
                && overload.canonical_name == result.canonical;
          });
      if (found != hir_.overload_sets().end()) {
        result.overloads = found->declarations;
        if (result.overloads.size() == 1) {
          result.selected = result.overloads.front();
        }
        break;
      }
      visible_scope = parent_scope(*visible_scope);
    }
    return result;
  }

  [[nodiscard]] vh::ExpressionKind expression_kind(
      const frontend::ExpressionKind kind) const noexcept {
    switch (kind) {
      case frontend::ExpressionKind::Invalid:
        return vh::ExpressionKind::invalid;
      case frontend::ExpressionKind::Identifier:
        return vh::ExpressionKind::name;
      case frontend::ExpressionKind::IntegerLiteral:
        return vh::ExpressionKind::integer_literal;
      case frontend::ExpressionKind::BooleanLiteral:
        return vh::ExpressionKind::boolean_literal;
      case frontend::ExpressionKind::LogicLiteral:
        return vh::ExpressionKind::logic_literal;
      case frontend::ExpressionKind::StringLiteral:
        return vh::ExpressionKind::string_literal;
      case frontend::ExpressionKind::Unary:
        return vh::ExpressionKind::unary;
      case frontend::ExpressionKind::Update:
        return vh::ExpressionKind::update;
      case frontend::ExpressionKind::Binary:
        return vh::ExpressionKind::binary;
      case frontend::ExpressionKind::Call:
        return vh::ExpressionKind::call;
      case frontend::ExpressionKind::Index:
        return vh::ExpressionKind::index;
      case frontend::ExpressionKind::Slice:
        return vh::ExpressionKind::slice;
      case frontend::ExpressionKind::Aggregate:
        return vh::ExpressionKind::aggregate;
      case frontend::ExpressionKind::Concatenation:
        return vh::ExpressionKind::concatenation;
      case frontend::ExpressionKind::Replication:
        return vh::ExpressionKind::replication;
      case frontend::ExpressionKind::DefaultChoice:
        return vh::ExpressionKind::default_choice;
    }
    return vh::ExpressionKind::invalid;
  }

  [[nodiscard]] std::optional<semantic::ExpressionId> expression(
      const frontend::Expression& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    if (input.kind == frontend::ExpressionKind::Invalid) {
      return std::nullopt;
    }
    const auto expression_source = source(input.span);
    const auto expression_origin = origin(
        expression_source, parent, "VHDL expression");
    const auto id = model_.add_expression_identity(
        scope, expression_source, expression_origin);
    vh::Expression output;
    output.id = id;
    output.scope = scope;
    output.kind = expression_kind(input.kind);
    output.text = input.text;
    output.source = expression_source;
    output.origin = expression_origin;
    output.nominal_type = input.nominal_type;
    output.decoded_string = input.decoded_string;
    if (input.kind == frontend::ExpressionKind::Identifier
        || input.kind == frontend::ExpressionKind::Call) {
      output.referenced_name = name(input.text, input.span, scope);
    }
    output.argument_names = input.call_argument_names;
    for (const auto& operand : input.operands) {
      if (const auto operand_id = expression(
              operand, scope, expression_origin)) {
        output.operands.push_back(*operand_id);
      }
    }
    if (input.kind == frontend::ExpressionKind::Aggregate) {
      for (std::size_t index = 0; index < output.operands.size(); ++index) {
        vh::AggregateAssociation association;
        association.value = output.operands[index];
        association.source = index < input.operands.size()
            ? source(input.operands[index].span)
            : expression_source;
        if (index < input.aggregate_choices.size()) {
          association.choice_spelling = input.aggregate_choices[index];
        }
        if (index < input.aggregate_choice_expressions.size()) {
          for (const auto& choice :
               input.aggregate_choice_expressions[index]) {
            if (const auto choice_id = expression(
                    choice, scope, expression_origin)) {
              association.choices.push_back(*choice_id);
            }
          }
        }
        output.associations.push_back(std::move(association));
      }
    }
    hir_.mutable_expressions().push_back(std::move(output));
    return id;
  }

  [[nodiscard]] vh::DelayValue delay_value(
      const std::uint64_t magnitude,
      const std::uint64_t divisor,
      const std::string& unit,
      const std::optional<frontend::Expression>& input_expression,
      const frontend::SourceSpan& span,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    return {
        magnitude,
        divisor,
        unit,
        input_expression
            ? expression(*input_expression, scope, parent)
            : std::nullopt,
        source(span)};
  }

  [[nodiscard]] vh::Delay delay(
      const frontend::Delay& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    vh::Delay output;
    output.primary = delay_value(
        input.magnitude,
        input.divisor,
        input.unit,
        input.expression,
        input.span,
        scope,
        parent);
    const auto alternative = [&](
        const std::optional<frontend::DelayAlternative>& value)
        -> std::optional<vh::DelayValue> {
      if (!value) {
        return std::nullopt;
      }
      return delay_value(
          value->magnitude,
          value->divisor,
          value->unit,
          value->expression,
          value->span,
          scope,
          parent);
    };
    output.minimum = alternative(input.minimum);
    output.typical = alternative(input.typical);
    output.maximum = alternative(input.maximum);
    for (const auto& additional : input.additional_values) {
      output.additional.push_back(delay_value(
          additional.magnitude,
          additional.divisor,
          additional.unit,
          additional.expression,
          additional.span,
          scope,
          parent));
    }
    return output;
  }

  [[nodiscard]] vh::Sensitivity sensitivity(
      const frontend::Sensitivity& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    return {
        input.signal,
        expression(input.expression, scope, parent),
        source(input.span)};
  }

  [[nodiscard]] vh::StatementKind statement_kind(
      const frontend::Statement& input) const noexcept {
    switch (input.kind) {
      case frontend::StatementKind::Assignment:
        return input.assignment_kind == frontend::AssignmentKind::Blocking
            ? vh::StatementKind::variable_assignment
            : vh::StatementKind::signal_assignment;
      case frontend::StatementKind::If:
        return vh::StatementKind::conditional;
      case frontend::StatementKind::Case:
        return vh::StatementKind::selection;
      case frontend::StatementKind::Loop:
        return vh::StatementKind::loop;
      case frontend::StatementKind::Break:
        return vh::StatementKind::exit_loop;
      case frontend::StatementKind::Continue:
        return vh::StatementKind::next_loop;
      case frontend::StatementKind::Return:
        return vh::StatementKind::return_statement;
      case frontend::StatementKind::ProcedureCall:
        return vh::StatementKind::procedure_call;
      case frontend::StatementKind::Assert:
        return vh::StatementKind::assertion;
      case frontend::StatementKind::Report:
        return vh::StatementKind::report;
      case frontend::StatementKind::Delay:
      case frontend::StatementKind::WaitOn:
      case frontend::StatementKind::WaitUntil:
        return vh::StatementKind::wait_statement;
      case frontend::StatementKind::Block:
        return vh::StatementKind::block;
      default:
        return vh::StatementKind::null_statement;
    }
  }

  [[nodiscard]] vh::DelayMechanism delay_mechanism(
      const frontend::VhdlDelayMechanism mechanism) const noexcept {
    switch (mechanism) {
      case frontend::VhdlDelayMechanism::ImplicitInertial:
        return vh::DelayMechanism::implicit_inertial;
      case frontend::VhdlDelayMechanism::Inertial:
        return vh::DelayMechanism::inertial;
      case frontend::VhdlDelayMechanism::Transport:
        return vh::DelayMechanism::transport;
    }
    return vh::DelayMechanism::implicit_inertial;
  }

  [[nodiscard]] semantic::TypeReference type_reference(
      const frontend::Type& input,
      const frontend::SourceSpan& fallback,
      const semantic::ScopeId scope) {
    const auto spelling = !input.vhdl_type_declaration.empty()
        ? input.vhdl_type_declaration
        : (!input.named_type.empty() ? input.named_type : input.spelling);
    semantic::TypeId target;
    auto visible_scope = std::optional<semantic::ScopeId>{scope};
    while (visible_scope && !target.valid()) {
      const auto found = std::ranges::find_if(
          model_.types(), [&](const semantic::Type& type) {
            return type.scope == *visible_scope
                && canonical_vhdl_name(type.name)
                    == canonical_vhdl_name(spelling);
          });
      if (found != model_.types().end()) {
        target = found->id;
      }
      visible_scope = parent_scope(*visible_scope);
    }
    return {target, source(fallback), spelling};
  }

  [[nodiscard]] semantic::DeclarationId block_variable(
      const frontend::VariableDeclaration& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    const auto variable_source = source(input.span);
    const auto variable_origin = origin(
        variable_source, parent, input.name);
    const auto common_kind = input.vhdl_file
        ? semantic::DeclarationKind::file
        : semantic::DeclarationKind::variable;
    const auto declaration_id = model_.add_declaration(
        scope, common_kind, input.name, variable_source, variable_origin);
    vh::Declaration output;
    output.id = declaration_id;
    output.scope = scope;
    output.form = input.vhdl_file
        ? vh::DeclarationForm::file
        : vh::DeclarationForm::variable;
    output.name = input.name;
    output.source = variable_source;
    output.origin = variable_origin;
    output.object_class = input.vhdl_file
        ? vh::ObjectClass::file
        : vh::ObjectClass::variable;
    output.shared = input.vhdl_shared;
    vh::SubtypeIndication subtype;
    subtype.type_mark = type_reference(input.type, input.span, scope);
    subtype.signed_value = input.type.is_signed;
    output.subtype = subtype;
    output.declared_value = model_.add_value(
        scope,
        semantic::ValueKind::variable,
        input.name,
        subtype.type_mark,
        variable_source,
        variable_origin);
    if (input.initializer) {
      output.initializer = expression(
          *input.initializer, scope, variable_origin);
    }
    hir_.mutable_declarations().push_back(std::move(output));
    return declaration_id;
  }

  [[nodiscard]] semantic::StatementId statement(
      const frontend::Statement& input,
      const semantic::ScopeId enclosing_scope,
      const semantic::OriginId parent) {
    const auto statement_source = source(input.span);
    const auto statement_origin = origin(
        statement_source, parent,
        input.label.empty() ? "VHDL statement" : input.label);
    const auto id = model_.add_statement_identity(
        enclosing_scope, statement_source, statement_origin);
    vh::Statement output;
    output.id = id;
    output.scope = enclosing_scope;
    output.kind = statement_kind(input);
    output.label = input.label;
    output.source = statement_source;
    output.origin = statement_origin;
    auto child_scope = enclosing_scope;
    if (!input.declarations.empty()) {
      const auto scope_name = input.label.empty() ? "<block>" : input.label;
      child_scope = model_.add_scope(
          model_.scopes()[enclosing_scope.value()].unit,
          enclosing_scope,
          scope_name,
          statement_source,
          statement_origin);
      output.nested_scope = child_scope;
      for (const auto& declaration : input.declarations) {
        output.declarations.push_back(block_variable(
            declaration, child_scope, statement_origin));
      }
    }
    output.target = expression(input.target, child_scope, statement_origin);
    output.value = expression(input.value, child_scope, statement_origin);
    output.condition = expression(
        input.condition, child_scope, statement_origin);
    output.procedure = name(
        input.procedure_name, input.span, child_scope);
    for (const auto& association : input.procedure_arguments) {
      const auto actual = expression(
          association.value, child_scope, statement_origin);
      if (!actual) {
        continue;
      }
      vh::ProcedureAssociation converted;
      if (association.formal) {
        converted.formal = name(
            *association.formal, association.span, child_scope);
      }
      converted.actual = *actual;
      converted.source = source(association.span);
      output.procedure_arguments.push_back(std::move(converted));
    }
    output.loop_variable = input.loop_variable;
    output.loop_label = input.loop_label;
    output.loop_control_label = input.loop_control_label;
    output.loop_initial = expression(
        input.loop_initial, child_scope, statement_origin);
    output.loop_limit = expression(
        input.loop_limit, child_scope, statement_origin);
    output.loop_descending = input.loop_descending;
    if (input.delay) {
      output.delay = delay(*input.delay, child_scope, statement_origin);
    }
    if (input.vhdl_delay_mechanism) {
      output.delay_mechanism = delay_mechanism(
          *input.vhdl_delay_mechanism);
    }
    if (input.vhdl_rejection_limit) {
      output.rejection_limit = delay(
          *input.vhdl_rejection_limit, child_scope, statement_origin);
    }
    output.postponed = input.vhdl_postponed;
    if (input.vhdl_disconnection_delay) {
      output.disconnection_delay = delay(
          *input.vhdl_disconnection_delay,
          child_scope,
          statement_origin);
    }
    for (const auto& waveform : input.vhdl_waveform) {
      const auto value = expression(
          waveform.value, child_scope, statement_origin);
      if (!value) {
        continue;
      }
      output.waveform.push_back({
          *value,
          waveform.delay
              ? std::optional<vh::Delay>{delay(
                    *waveform.delay, child_scope, statement_origin)}
              : std::nullopt,
          waveform.disconnect,
          source(waveform.span)});
    }
    output.unaffected = input.vhdl_unaffected;
    for (const auto& item : input.sensitivities) {
      output.sensitivities.push_back(sensitivity(
          item, child_scope, statement_origin));
    }
    output.report = expression(
        input.vhdl_report_expression, child_scope, statement_origin);
    output.severity = expression(
        input.vhdl_severity_expression, child_scope, statement_origin);
    for (const auto& child : input.statements) {
      output.statements.push_back(statement(
          child, child_scope, statement_origin));
    }
    for (const auto& child : input.else_statements) {
      output.else_statements.push_back(statement(
          child, child_scope, statement_origin));
    }
    for (const auto& alternative : input.case_alternatives) {
      vh::CaseAlternative converted;
      converted.is_default = alternative.is_default;
      converted.source = source(alternative.span);
      for (const auto& choice : alternative.choices) {
        if (const auto choice_id = expression(
                choice, child_scope, statement_origin)) {
          converted.choices.push_back(*choice_id);
        }
      }
      for (const auto& child : alternative.statements) {
        converted.statements.push_back(statement(
            child, child_scope, statement_origin));
      }
      output.alternatives.push_back(std::move(converted));
    }
    hir_.mutable_statements().push_back(std::move(output));
    return id;
  }

  [[nodiscard]] vh::Process& process(const semantic::ProcessId id) {
    const auto found = std::ranges::find_if(
        hir_.mutable_processes(), [&](const vh::Process& process) {
          return process.id == id;
        });
    if (found == hir_.mutable_processes().end()) {
      throw std::logic_error{"missing VHDL HIR process"};
    }
    return *found;
  }

  void fill_process(
      const frontend::Process& input,
      const semantic::ProcessId id) {
    auto& output = process(id);
    const auto process_scope = output.scope;
    const auto process_origin = output.origin;
    for (const auto& item : input.sensitivities) {
      output.sensitivities.push_back(sensitivity(
          item, process_scope, process_origin));
    }
    for (const auto& item : input.statements) {
      output.statements.push_back(statement(
          item, process_scope, process_origin));
    }
    fill_callable_bodies(
        input.functions, input.procedures, process_scope);
    fill_protected_types(input.type_aliases, process_scope);
  }

  [[nodiscard]] vh::Declaration* find_callable(
      const std::string_view callable_name,
      const frontend::SourceSpan& span,
      const semantic::ScopeId scope,
      const vh::DeclarationForm form) {
    const auto callable_source = source(span);
    const auto found = std::ranges::find_if(
        hir_.mutable_declarations(), [&](const vh::Declaration& declaration) {
          return declaration.scope == scope
              && declaration.form == form
              && declaration.source == callable_source
              && canonical_vhdl_name(declaration.name)
                  == canonical_vhdl_name(std::string{callable_name});
        });
    return found == hir_.mutable_declarations().end()
        ? nullptr
        : &*found;
  }

  void fill_callable_bodies(
      const std::span<const frontend::FunctionDeclaration> functions,
      const std::span<const frontend::ProcedureDeclaration> procedures,
      const semantic::ScopeId scope) {
    for (const auto& input : functions) {
      auto* output = find_callable(
          input.name, input.span, scope, vh::DeclarationForm::function);
      if (output == nullptr || !output->nested_scope) {
        continue;
      }
      const auto id = output->id;
      const auto callable_scope = *output->nested_scope;
      const auto callable_origin = output->origin;
      std::vector<semantic::StatementId> statements;
      for (const auto& item : input.statements) {
        statements.push_back(statement(
            item, callable_scope, callable_origin));
      }
      const auto refreshed = std::ranges::find_if(
          hir_.mutable_declarations(), [&](const vh::Declaration& candidate) {
            return candidate.id == id;
          });
      refreshed->statements = std::move(statements);
      fill_callable_bodies(
          input.functions, input.procedures, callable_scope);
      fill_protected_types(input.type_aliases, callable_scope);
    }
    for (const auto& input : procedures) {
      auto* output = find_callable(
          input.name, input.span, scope, vh::DeclarationForm::procedure);
      if (output == nullptr || !output->nested_scope) {
        continue;
      }
      const auto id = output->id;
      const auto callable_scope = *output->nested_scope;
      const auto callable_origin = output->origin;
      std::vector<semantic::StatementId> statements;
      for (const auto& item : input.statements) {
        statements.push_back(statement(
            item, callable_scope, callable_origin));
      }
      const auto refreshed = std::ranges::find_if(
          hir_.mutable_declarations(), [&](const vh::Declaration& candidate) {
            return candidate.id == id;
          });
      refreshed->statements = std::move(statements);
      fill_callable_bodies(
          input.functions, input.procedures, callable_scope);
      fill_protected_types(input.type_aliases, callable_scope);
    }
  }

  void fill_protected_types(
      const std::span<const frontend::TypeAliasDeclaration> types,
      const semantic::ScopeId scope) {
    for (const auto& input : types) {
      if (!input.type.vhdl_protected) {
        continue;
      }
      const auto input_source = source(input.span);
      const auto found = std::ranges::find_if(
          hir_.declarations(), [&](const vh::Declaration& declaration) {
            return declaration.scope == scope
                && declaration.form == vh::DeclarationForm::type
                && declaration.source == input_source
                && canonical_vhdl_name(declaration.name)
                    == canonical_vhdl_name(input.name);
          });
      if (found == hir_.declarations().end() || !found->nested_scope) {
        continue;
      }
      fill_callable_bodies(
          input.type.vhdl_protected->functions,
          input.type.vhdl_protected->procedures,
          *found->nested_scope);
    }
  }

  void fill_generic_templates(
      const std::span<const frontend::GenericFunctionTemplate> functions,
      const std::span<const frontend::GenericProcedureTemplate> procedures,
      const semantic::ScopeId scope) {
    for (const auto& input : functions) {
      auto* declaration = find_callable(
          input.function.name,
          input.span,
          scope,
          vh::DeclarationForm::generic_function_template);
      if (declaration != nullptr && declaration->nested_scope) {
        const std::span<const frontend::FunctionDeclaration> function{
            &input.function, 1};
        fill_callable_bodies(function, {}, *declaration->nested_scope);
      }
    }
    for (const auto& input : procedures) {
      auto* declaration = find_callable(
          input.procedure.name,
          input.span,
          scope,
          vh::DeclarationForm::generic_procedure_template);
      if (declaration != nullptr && declaration->nested_scope) {
        const std::span<const frontend::ProcedureDeclaration> procedure{
            &input.procedure, 1};
        fill_callable_bodies({}, procedure, *declaration->nested_scope);
      }
    }
  }

  void fill_generate_body(
      const frontend::GenerateBody& input,
      vh::GenerateRegion& output) {
    for (std::size_t index = 0;
         index < input.processes.size() && index < output.processes.size();
         ++index) {
      fill_process(input.processes[index], output.processes[index]);
    }
    for (const auto& item : input.concurrent_statements) {
      output.concurrent_statements.push_back(statement(
          item, output.scope, output.origin));
    }
    fill_callable_bodies(input.functions, input.procedures, output.scope);
    fill_generic_templates(
        input.generic_function_templates,
        input.generic_procedure_templates,
        output.scope);
    fill_protected_types(input.type_aliases, output.scope);
    for (const auto& nested_input : input.generate_regions) {
      const auto nested_source = source(nested_input.span);
      const auto nested = std::ranges::find_if(
          output.nested, [&](const vh::GenerateRegion& candidate) {
            return candidate.source == nested_source;
          });
      if (nested != output.nested.end()) {
        fill_generate(nested_input, *nested);
      }
    }
  }

  void fill_generate(
      const frontend::GenerateRegion& input,
      vh::GenerateRegion& output) {
    fill_generate_body(input.then_body, output);
    for (const auto& alternative : input.alternatives) {
      const auto alternative_source = source(alternative.span);
      const auto found = std::ranges::find_if(
          output.nested, [&](const vh::GenerateRegion& candidate) {
            return candidate.source == alternative_source;
          });
      if (found != output.nested.end()) {
        fill_generate_body(alternative.body, *found);
      }
    }
    if (!input.else_scope.empty()
        || !input.else_body.processes.empty()
        || !input.else_body.concurrent_statements.empty()) {
      const auto found = std::ranges::find_if(
          output.nested, [&](const vh::GenerateRegion& candidate) {
            return candidate.label == input.else_scope
                || candidate.source == source(input.span);
          });
      if (found != output.nested.end()) {
        fill_generate_body(input.else_body, *found);
      }
    }
  }

  void fill_unit(
      const frontend::DesignUnit& input,
      vh::Unit& output) {
    for (std::size_t index = 0;
         index < input.processes.size() && index < output.processes.size();
         ++index) {
      fill_process(input.processes[index], output.processes[index]);
    }
    for (const auto& item : input.concurrent_statements) {
      output.concurrent_statements.push_back(statement(
          item, output.scope, output.origin));
    }
    fill_callable_bodies(input.functions, input.procedures, output.scope);
    fill_generic_templates(
        input.generic_function_templates,
        input.generic_procedure_templates,
        output.scope);
    fill_protected_types(input.type_aliases, output.scope);
    for (std::size_t index = 0;
         index < input.generate_regions.size()
         && index < output.generates.size();
         ++index) {
      fill_generate(input.generate_regions[index], output.generates[index]);
    }
  }

  void rebuild_overload_sets() {
    std::map<std::pair<semantic::ScopeId, std::string>,
             std::vector<semantic::DeclarationId>> groups;
    for (const auto& declaration : hir_.declarations()) {
      groups[{declaration.scope, canonical_vhdl_name(declaration.name)}]
          .push_back(declaration.id);
    }
    auto& overloads = hir_.mutable_overload_sets();
    overloads.clear();
    for (auto& [key, declarations] : groups) {
      overloads.push_back(
          {key.first, std::move(key.second), std::move(declarations)});
    }
  }

  void resolve_expression_names() {
    for (auto& expression : hir_.mutable_expressions()) {
      if (!expression.referenced_name) {
        continue;
      }
      auto visible_scope = std::optional<semantic::ScopeId>{expression.scope};
      while (visible_scope) {
        const auto found = std::ranges::find_if(
            hir_.overload_sets(), [&](const vh::OverloadSet& overload) {
              return overload.scope == *visible_scope
                  && overload.canonical_name
                      == expression.referenced_name->canonical;
            });
        if (found != hir_.overload_sets().end()) {
          expression.referenced_name->overloads = found->declarations;
          if (found->declarations.size() == 1) {
            expression.referenced_name->selected =
                found->declarations.front();
          }
          break;
        }
        visible_scope = parent_scope(*visible_scope);
      }
    }
  }

  semantic::Model& model_;
  vh::Hir& hir_;
};

} // namespace

void complete_vhdl_executable_hir(
    const frontend::ParsedDesign& parsed,
    semantic::Model& semantics,
    semantic::vhdl::Hir& hir) {
  VhdlExecutableBuilder builder{semantics, hir};
  builder.add_design(parsed);
}

} // namespace fsim::app::application_detail
