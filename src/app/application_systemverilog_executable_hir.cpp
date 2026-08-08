// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include <concepts>

namespace fsim::app::application_detail {
namespace {

namespace sv = semantic::sv;

class SystemVerilogExecutableBuilder final {
 public:
  SystemVerilogExecutableBuilder(semantic::Model& model, sv::Hir& hir)
      : model_(model), hir_(hir) {}

  void add_design(const frontend::ParsedDesign& parsed) {
    for (std::size_t index = 0; index < parsed.units.size(); ++index) {
      const auto& input = parsed.units[index];
      if (input.language == frontend::Language::Vhdl2008) {
        continue;
      }
      const auto unit_id = semantic::UnitId::from_index(
          static_cast<std::uint32_t>(index));
      const auto found = std::ranges::find_if(
          hir_.mutable_units(), [&](const sv::Unit& unit) {
            return unit.id == unit_id;
          });
      if (found == hir_.mutable_units().end()) {
        throw std::logic_error{"missing SystemVerilog HIR unit"};
      }
      fill_unit(input, *found);
    }
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

  [[nodiscard]] semantic::UnitId scope_unit(
      const semantic::ScopeId scope) const noexcept {
    return model_.scopes()[scope.value()].unit;
  }

  [[nodiscard]] sv::Name name(
      const std::string_view spelling,
      const frontend::SourceSpan& span,
      const semantic::ScopeId scope) {
    sv::Name result{std::string{spelling}, source(span), std::nullopt, {}};
    auto visible_scope = std::optional<semantic::ScopeId>{scope};
    while (visible_scope) {
      for (const auto& declaration : hir_.declarations()) {
        if (declaration.scope == *visible_scope
            && declaration.name == spelling) {
          result.overloads.push_back(declaration.id);
        }
      }
      if (!result.overloads.empty()) {
        break;
      }
      visible_scope = parent_scope(*visible_scope);
    }
    if (result.overloads.size() == 1) {
      result.selected = result.overloads.front();
    }
    return result;
  }

  [[nodiscard]] sv::ExpressionKind expression_kind(
      const frontend::Expression& expression) const noexcept {
    if (expression.text == "@sv-null") {
      return sv::ExpressionKind::class_null;
    }
    if (expression.text.starts_with("@sv-new:")) {
      return sv::ExpressionKind::class_allocation;
    }
    if (expression.text.starts_with("@sv-dollar-cast:")) {
      return sv::ExpressionKind::class_cast;
    }
    if (expression.text.starts_with("@sv-property:")) {
      return sv::ExpressionKind::class_property;
    }
    if (expression.text.starts_with("@sv-static-property:")) {
      return sv::ExpressionKind::class_static_property;
    }
    if (expression.text.starts_with("@sv-method:")
        || expression.text.starts_with("@sv-base-method:")) {
      return sv::ExpressionKind::class_method_call;
    }
    if (expression.text.starts_with("@sv-static-method:")) {
      return sv::ExpressionKind::class_static_method_call;
    }
    switch (expression.kind) {
      case frontend::ExpressionKind::Invalid:
        return sv::ExpressionKind::invalid;
      case frontend::ExpressionKind::Identifier:
        return sv::ExpressionKind::name;
      case frontend::ExpressionKind::IntegerLiteral:
        return sv::ExpressionKind::integer_literal;
      case frontend::ExpressionKind::BooleanLiteral:
        return sv::ExpressionKind::boolean_literal;
      case frontend::ExpressionKind::LogicLiteral:
        return sv::ExpressionKind::logic_literal;
      case frontend::ExpressionKind::StringLiteral:
        return sv::ExpressionKind::string_literal;
      case frontend::ExpressionKind::Unary:
        return sv::ExpressionKind::unary;
      case frontend::ExpressionKind::Update:
        return sv::ExpressionKind::update;
      case frontend::ExpressionKind::Binary:
        return sv::ExpressionKind::binary;
      case frontend::ExpressionKind::Call:
        return sv::ExpressionKind::call;
      case frontend::ExpressionKind::Index:
        return sv::ExpressionKind::index;
      case frontend::ExpressionKind::Slice:
        return sv::ExpressionKind::slice;
      case frontend::ExpressionKind::Aggregate:
        return sv::ExpressionKind::assignment_pattern;
      case frontend::ExpressionKind::Concatenation:
        return sv::ExpressionKind::concatenation;
      case frontend::ExpressionKind::Replication:
        return sv::ExpressionKind::replication;
      case frontend::ExpressionKind::DefaultChoice:
        return sv::ExpressionKind::default_choice;
    }
    return sv::ExpressionKind::invalid;
  }

  [[nodiscard]] std::pair<semantic::ExpressionId, semantic::OriginId>
  expression_identity(
      const semantic::ScopeId scope,
      const semantic::SourceSpanId expression_source,
      const semantic::OriginId parent) {
    for (const auto& identity : model_.expression_identities()) {
      if (identity.scope != scope || identity.source != expression_source
          || claimed_expressions_.contains(identity.id.value())) {
        continue;
      }
      const auto& candidate_origin = model_.origins()[identity.origin.value()];
      if (candidate_origin.parent == parent
          && candidate_origin.detail == "SystemVerilog expression") {
        claimed_expressions_.insert(identity.id.value());
        return {identity.id, identity.origin};
      }
    }
    const auto expression_origin = origin(
        expression_source, parent, "SystemVerilog expression");
    const auto id = model_.add_expression_identity(
        scope, expression_source, expression_origin);
    claimed_expressions_.insert(id.value());
    return {id, expression_origin};
  }

  [[nodiscard]] std::optional<semantic::ExpressionId> expression(
      const frontend::Expression& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    if (input.kind == frontend::ExpressionKind::Invalid) {
      return std::nullopt;
    }
    const auto expression_source = source(input.span);
    const auto [id, expression_origin] = expression_identity(
        scope, expression_source, parent);
    sv::Expression output;
    output.id = id;
    output.scope = scope;
    output.kind = expression_kind(input);
    output.text = input.text;
    output.source = expression_source;
    output.origin = expression_origin;
    output.nominal_type = input.nominal_type;
    output.class_identity = input.nominal_type;
    const auto bind_class_operation = [&](const std::string_view prefix) {
      if (!input.text.starts_with(prefix)) return false;
      const auto payload = input.text.substr(prefix.size());
      output.class_member_identity = payload;
      if (output.class_identity.empty()) {
        const auto separator = payload.rfind("::");
        output.class_identity = separator == std::string::npos
            ? payload
            : payload.substr(0, separator);
      }
      return true;
    };
    if (!bind_class_operation("@sv-new:")
        && !bind_class_operation("@sv-dollar-cast:")
        && !bind_class_operation("@sv-property:")
        && !bind_class_operation("@sv-static-property:")
        && !bind_class_operation("@sv-base-method:")
        && !bind_class_operation("@sv-method:")
        && !bind_class_operation("@sv-static-method:")) {
      output.class_member_identity.clear();
    }
    output.class_checked = output.kind == sv::ExpressionKind::class_cast
        || output.kind == sv::ExpressionKind::class_property
        || output.kind == sv::ExpressionKind::class_method_call;
    output.decoded_string = input.decoded_string;
    if (input.kind == frontend::ExpressionKind::Identifier
        || (input.kind == frontend::ExpressionKind::Call
            && !input.text.starts_with("@sv-"))) {
      output.referenced_name = name(input.text, input.span, scope);
    }
    output.argument_names = input.call_argument_names;
    for (std::size_t index = 0; index < input.operands.size(); ++index) {
      const auto& operand = input.operands[index];
      const auto operand_id = expression(operand, scope, expression_origin);
      if (operand_id) {
        output.operands.push_back(*operand_id);
      }
      if (input.kind == frontend::ExpressionKind::Call) {
        sv::CallAssociation association;
        if (index < input.call_argument_names.size()
            && !input.call_argument_names[index].empty()) {
          association.formal = input.call_argument_names[index];
        }
        association.actual = operand_id;
        association.source = operand.valid()
            ? source(operand.span)
            : expression_source;
        output.call_arguments.push_back(std::move(association));
      }
    }
    if (input.kind == frontend::ExpressionKind::Aggregate) {
      for (std::size_t index = 0; index < output.operands.size(); ++index) {
        sv::AssignmentPatternAssociation association;
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

  [[nodiscard]] sv::DelayValue delay_value(
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

  [[nodiscard]] sv::Delay delay(
      const frontend::Delay& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    sv::Delay output;
    output.primary = delay_value(
        input.magnitude, input.divisor, input.unit, input.expression,
        input.span, scope, parent);
    const auto alternative = [&] (
        const std::optional<frontend::DelayAlternative>& value)
        -> std::optional<sv::DelayValue> {
      if (!value) {
        return std::nullopt;
      }
      return delay_value(
          value->magnitude, value->divisor, value->unit,
          value->expression, value->span, scope, parent);
    };
    output.minimum = alternative(input.minimum);
    output.typical = alternative(input.typical);
    output.maximum = alternative(input.maximum);
    for (const auto& additional : input.additional_values) {
      output.additional.push_back(delay_value(
          additional.magnitude, additional.divisor, additional.unit,
          additional.expression, additional.span, scope, parent));
    }
    return output;
  }

  [[nodiscard]] sv::EdgeKind edge_kind(
      const frontend::EdgeKind kind) const noexcept {
    switch (kind) {
      case frontend::EdgeKind::Any:
        return sv::EdgeKind::any;
      case frontend::EdgeKind::Positive:
        return sv::EdgeKind::positive;
      case frontend::EdgeKind::Negative:
        return sv::EdgeKind::negative;
    }
    return sv::EdgeKind::any;
  }

  [[nodiscard]] sv::Sensitivity sensitivity(
      const frontend::Sensitivity& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    return {
        edge_kind(input.edge),
        input.signal,
        expression(input.expression, scope, parent),
        source(input.span)};
  }

  [[nodiscard]] sv::StatementKind statement_kind(
      const frontend::StatementKind kind) const noexcept {
    switch (kind) {
      case frontend::StatementKind::Assignment:
        return sv::StatementKind::assignment;
      case frontend::StatementKind::Force:
        return sv::StatementKind::force;
      case frontend::StatementKind::Release:
        return sv::StatementKind::release;
      case frontend::StatementKind::If:
        return sv::StatementKind::conditional;
      case frontend::StatementKind::Case:
        return sv::StatementKind::selection;
      case frontend::StatementKind::Loop:
        return sv::StatementKind::loop;
      case frontend::StatementKind::Break:
        return sv::StatementKind::break_loop;
      case frontend::StatementKind::Continue:
        return sv::StatementKind::continue_loop;
      case frontend::StatementKind::Return:
        return sv::StatementKind::return_statement;
      case frontend::StatementKind::TaskCall:
      case frontend::StatementKind::ProcedureCall:
        return sv::StatementKind::task_call;
      case frontend::StatementKind::Assert:
        return sv::StatementKind::assertion;
      case frontend::StatementKind::Delay:
        return sv::StatementKind::delay_control;
      case frontend::StatementKind::WaitOn:
        return sv::StatementKind::event_control;
      case frontend::StatementKind::WaitUntil:
        return sv::StatementKind::wait_statement;
      case frontend::StatementKind::EventTrigger:
        return sv::StatementKind::event_trigger;
      case frontend::StatementKind::Fork:
        return sv::StatementKind::fork;
      case frontend::StatementKind::WaitFork:
        return sv::StatementKind::wait_fork;
      case frontend::StatementKind::DisableFork:
        return sv::StatementKind::disable_fork;
      case frontend::StatementKind::Display:
      case frontend::StatementKind::Report:
        return sv::StatementKind::display;
      case frontend::StatementKind::FileClose:
        return sv::StatementKind::file_close;
      case frontend::StatementKind::FileFlush:
        return sv::StatementKind::file_flush;
      case frontend::StatementKind::FileDisplay:
        return sv::StatementKind::file_display;
      case frontend::StatementKind::MemoryLoad:
        return sv::StatementKind::memory_transfer;
      case frontend::StatementKind::ContainerMethod:
        return sv::StatementKind::container_method;
      case frontend::StatementKind::MonitorControl:
        return sv::StatementKind::monitor_control;
      case frontend::StatementKind::Pause:
        return sv::StatementKind::pause;
      case frontend::StatementKind::Finish:
        return sv::StatementKind::finish;
      case frontend::StatementKind::Block:
        return sv::StatementKind::block;
      case frontend::StatementKind::Null:
        return sv::StatementKind::null_statement;
    }
    return sv::StatementKind::null_statement;
  }

  [[nodiscard]] sv::AssignmentKind assignment_kind(
      const frontend::AssignmentKind kind) const noexcept {
    switch (kind) {
      case frontend::AssignmentKind::Blocking:
        return sv::AssignmentKind::blocking;
      case frontend::AssignmentKind::NonBlocking:
        return sv::AssignmentKind::nonblocking;
      case frontend::AssignmentKind::Continuous:
      case frontend::AssignmentKind::VhdlSignal:
        return sv::AssignmentKind::continuous;
    }
    return sv::AssignmentKind::blocking;
  }

  [[nodiscard]] sv::AssignmentControl assignment_control(
      const frontend::ProceduralAssignmentControl control) const noexcept {
    switch (control) {
      case frontend::ProceduralAssignmentControl::None:
        return sv::AssignmentControl::none;
      case frontend::ProceduralAssignmentControl::Delay:
        return sv::AssignmentControl::delay;
      case frontend::ProceduralAssignmentControl::Event:
        return sv::AssignmentControl::event;
    }
    return sv::AssignmentControl::none;
  }

  [[nodiscard]] sv::UpdateKind update_kind(
      const frontend::ProceduralUpdateKind kind) const noexcept {
    switch (kind) {
      case frontend::ProceduralUpdateKind::None:
        return sv::UpdateKind::none;
      case frontend::ProceduralUpdateKind::Compound:
        return sv::UpdateKind::compound;
      case frontend::ProceduralUpdateKind::Prefix:
        return sv::UpdateKind::prefix;
      case frontend::ProceduralUpdateKind::Postfix:
        return sv::UpdateKind::postfix;
    }
    return sv::UpdateKind::none;
  }

  [[nodiscard]] sv::ForkJoinKind fork_join_kind(
      const frontend::ForkJoinKind kind) const noexcept {
    switch (kind) {
      case frontend::ForkJoinKind::All:
        return sv::ForkJoinKind::all;
      case frontend::ForkJoinKind::Any:
        return sv::ForkJoinKind::any;
      case frontend::ForkJoinKind::None:
        return sv::ForkJoinKind::none;
    }
    return sv::ForkJoinKind::all;
  }

  [[nodiscard]] sv::CaseMatchKind case_match_kind(
      const frontend::CaseMatchKind kind) const noexcept {
    switch (kind) {
      case frontend::CaseMatchKind::Exact:
        return sv::CaseMatchKind::exact;
      case frontend::CaseMatchKind::WildcardZ:
        return sv::CaseMatchKind::wildcard_z;
      case frontend::CaseMatchKind::WildcardXZ:
        return sv::CaseMatchKind::wildcard_xz;
      case frontend::CaseMatchKind::Inside:
        return sv::CaseMatchKind::inside;
      case frontend::CaseMatchKind::Matches:
        return sv::CaseMatchKind::matches;
      case frontend::CaseMatchKind::VhdlMatching:
        return sv::CaseMatchKind::exact;
    }
    return sv::CaseMatchKind::exact;
  }

  [[nodiscard]] sv::CaseQualifier case_qualifier(
      const frontend::CaseQualifier qualifier) const noexcept {
    switch (qualifier) {
      case frontend::CaseQualifier::None:
        return sv::CaseQualifier::none;
      case frontend::CaseQualifier::Unique:
        return sv::CaseQualifier::unique;
      case frontend::CaseQualifier::Unique0:
        return sv::CaseQualifier::unique0;
      case frontend::CaseQualifier::Priority:
        return sv::CaseQualifier::priority;
    }
    return sv::CaseQualifier::none;
  }

  [[nodiscard]] sv::AssertionSeverity assertion_severity(
      const frontend::AssertionSeverity severity) const noexcept {
    switch (severity) {
      case frontend::AssertionSeverity::Note:
        return sv::AssertionSeverity::note;
      case frontend::AssertionSeverity::Warning:
        return sv::AssertionSeverity::warning;
      case frontend::AssertionSeverity::Error:
        return sv::AssertionSeverity::error;
      case frontend::AssertionSeverity::Failure:
        return sv::AssertionSeverity::failure;
    }
    return sv::AssertionSeverity::error;
  }

  [[nodiscard]] sv::OutputFormat output_format(
      const frontend::OutputFormat format) const noexcept {
    switch (format) {
      case frontend::OutputFormat::Binary:
        return sv::OutputFormat::binary;
      case frontend::OutputFormat::Hexadecimal:
        return sv::OutputFormat::hexadecimal;
      case frontend::OutputFormat::Octal:
        return sv::OutputFormat::octal;
      case frontend::OutputFormat::Decimal:
        return sv::OutputFormat::decimal;
      case frontend::OutputFormat::Character:
        return sv::OutputFormat::character;
      case frontend::OutputFormat::String:
        return sv::OutputFormat::string;
      case frontend::OutputFormat::RealScientific:
        return sv::OutputFormat::real_scientific;
      case frontend::OutputFormat::RealFixed:
        return sv::OutputFormat::real_fixed;
      case frontend::OutputFormat::RealGeneral:
        return sv::OutputFormat::real_general;
      case frontend::OutputFormat::Hierarchy:
        return sv::OutputFormat::hierarchy;
      case frontend::OutputFormat::Time:
        return sv::OutputFormat::time;
    }
    return sv::OutputFormat::decimal;
  }

  [[nodiscard]] semantic::TypeId find_type(
      const semantic::ScopeId scope,
      const std::string_view spelling) const noexcept {
    auto visible_scope = std::optional<semantic::ScopeId>{scope};
    while (visible_scope) {
      const auto found = std::ranges::find_if(
          model_.types(), [&](const semantic::Type& type) {
            return type.scope == *visible_scope && type.name == spelling;
          });
      if (found != model_.types().end()) {
        return found->id;
      }
      visible_scope = parent_scope(*visible_scope);
    }
    return {};
  }

  [[nodiscard]] sv::TypeReference type_reference(
      const frontend::Type& input,
      const frontend::SourceSpan& fallback,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    const auto spelling = input.named_type.empty()
        ? input.spelling
        : input.named_type;
    sv::TypeReference output;
    output.target = {
        input.named_type.empty() ? semantic::TypeId{}
                                 : find_type(scope, input.named_type),
        source(input.named_type.empty() ? fallback : input.named_type_span),
        spelling};
    if (!input.systemverilog_class_declaration.empty()) {
      output.value_form = sv::TypeForm::class_handle;
      output.class_identity = input.systemverilog_class_declaration;
    }
    output.signed_value = input.is_signed;
    if (input.packed_range) {
      output.packed_range = sv::PackedRange{
          input.packed_range->left,
          input.packed_range->right,
          std::nullopt,
          std::nullopt,
          input.packed_range->descending,
          source(fallback)};
    } else if (input.packed_range_expression) {
      const auto& range = *input.packed_range_expression;
      output.packed_range = sv::PackedRange{
          std::nullopt,
          std::nullopt,
          expression(range.left, scope, parent),
          expression(range.right, scope, parent),
          range.descending.value_or(true),
          source(range.span)};
    }
    if (input.systemverilog_container) {
      const auto& container = *input.systemverilog_container;
      switch (container.kind) {
        case frontend::SystemVerilogContainerKind::DynamicArray:
          output.container_form = sv::TypeForm::dynamic_array;
          break;
        case frontend::SystemVerilogContainerKind::Queue:
          output.container_form = sv::TypeForm::queue;
          break;
        case frontend::SystemVerilogContainerKind::AssociativeArray:
          output.container_form = sv::TypeForm::associative_array;
          break;
        case frontend::SystemVerilogContainerKind::StaticArray:
          output.container_form = sv::TypeForm::static_array;
          break;
      }
      output.queue_maximum = container.queue_maximum
          ? expression(*container.queue_maximum, scope, parent)
          : std::nullopt;
      if (container.associative_index_type) {
        output.associative_index = type_reference(
            *container.associative_index_type, container.span,
            scope, parent).target;
      }
      for (const auto& range : container.static_range_expressions) {
        output.unpacked_dimensions.push_back({
            std::nullopt,
            std::nullopt,
            expression(range.left, scope, parent),
            expression(range.right, scope, parent),
            range.descending.value_or(true),
            source(range.span)});
      }
      if (container.static_range_expressions.empty()
          && container.static_range) {
        output.unpacked_dimensions.push_back({
            container.static_range->left,
            container.static_range->right,
            std::nullopt,
            std::nullopt,
            container.static_range->descending,
            source(container.span)});
      }
    }
    return output;
  }

  void fill_type_expressions(
      const frontend::Type& input,
      const frontend::SourceSpan& fallback,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    static_cast<void>(type_reference(input, fallback, scope, parent));
    for (const auto& member : input.packed_members) {
      if (!member.nested_types.empty()) {
        fill_type_expressions(
            member.nested_types.front(), member.span, scope, parent);
      } else if (member.packed_range_expression) {
        static_cast<void>(expression(
            member.packed_range_expression->left, scope, parent));
        static_cast<void>(expression(
            member.packed_range_expression->right, scope, parent));
      }
    }
  }

  [[nodiscard]] sv::Declaration* find_declaration(
      const semantic::ScopeId scope,
      const sv::DeclarationForm form,
      const std::string_view declaration_name,
      const frontend::SourceSpan& span) {
    const auto declaration_source = source(span);
    const auto found = std::ranges::find_if(
        hir_.mutable_declarations(), [&](const sv::Declaration& declaration) {
          return declaration.scope == scope && declaration.form == form
              && declaration.name == declaration_name
              && declaration.source == declaration_source;
        });
    return found == hir_.mutable_declarations().end() ? nullptr : &*found;
  }

  [[nodiscard]] semantic::DeclarationId block_variable(
      const frontend::VariableDeclaration& input,
      const semantic::ScopeId scope,
      const semantic::OriginId parent) {
    const auto variable_source = source(input.span);
    const auto variable_origin = origin(variable_source, parent, input.name);
    const auto declaration_id = model_.add_declaration(
        scope, semantic::DeclarationKind::variable, input.name,
        variable_source, variable_origin);
    sv::Declaration output;
    output.id = declaration_id;
    output.scope = scope;
    output.form = sv::DeclarationForm::variable;
    output.name = input.name;
    output.source = variable_source;
    output.origin = variable_origin;
    output.type = type_reference(
        input.type, input.span, scope, variable_origin);
    output.declared_value = model_.add_value(
        scope,
        semantic::ValueKind::variable,
        input.name,
        output.type->target,
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
        input.label.empty() ? "SystemVerilog statement" : input.label);
    const auto id = model_.add_statement_identity(
        enclosing_scope, statement_source, statement_origin);
    sv::Statement output;
    output.id = id;
    output.scope = enclosing_scope;
    output.kind = statement_kind(input.kind);
    output.label = input.label;
    output.source = statement_source;
    output.origin = statement_origin;
    auto child_scope = enclosing_scope;
    if (input.kind == frontend::StatementKind::Block
        || input.kind == frontend::StatementKind::Fork
        || !input.declarations.empty()) {
      const auto scope_name = input.label.empty()
          ? (input.kind == frontend::StatementKind::Fork
                 ? "<fork>"
                 : "<block>")
          : input.label;
      child_scope = model_.add_scope(
          scope_unit(enclosing_scope), enclosing_scope, scope_name,
          statement_source, statement_origin);
      output.nested_scope = child_scope;
      for (const auto& declaration : input.declarations) {
        output.declarations.push_back(block_variable(
            declaration, child_scope, statement_origin));
      }
    }
    output.assignment_kind = assignment_kind(input.assignment_kind);
    output.target = expression(input.target, child_scope, statement_origin);
    output.value = expression(input.value, child_scope, statement_origin);
    output.condition = expression(
        input.condition, child_scope, statement_origin);
    output.task = name(input.task_name, input.span, child_scope);
    for (std::size_t index = 0; index < input.task_arguments.size(); ++index) {
      const auto& argument = input.task_arguments[index];
      sv::TaskAssociation association;
      if (index < input.task_argument_names.size()
          && !input.task_argument_names[index].empty()) {
        association.formal = input.task_argument_names[index];
      }
      association.actual = expression(
          argument, child_scope, statement_origin);
      association.source = argument.valid()
          ? source(argument.span)
          : statement_source;
      output.task_arguments.push_back(std::move(association));
    }
    output.loop_variable = input.loop_variable;
    output.loop_variable_declared = input.loop_variable_declared;
    output.loop_initial = expression(
        input.loop_initial, child_scope, statement_origin);
    output.loop_limit = expression(
        input.loop_limit, child_scope, statement_origin);
    output.loop_update_target = expression(
        input.loop_update_target, child_scope, statement_origin);
    for (const auto& update : input.loop_updates) {
      output.loop_updates.push_back(statement(
          update, child_scope, statement_origin));
    }
    output.loop_descending = input.loop_descending;
    output.loop_limit_exclusive = input.loop_limit_exclusive;
    output.loop_repeat = input.loop_repeat;
    output.loop_runtime = input.loop_runtime;
    output.loop_post_test = input.loop_post_test;
    output.fork_join = fork_join_kind(input.fork_join_kind);
    output.assignment_control = assignment_control(
        input.procedural_assignment_control);
    output.assignment_control_repeated = input.procedural_assignment_repeat;
    output.update_kind = update_kind(input.procedural_update_kind);
    output.update_operator = input.procedural_update_operator;
    if (input.delay) {
      output.delay = delay(*input.delay, child_scope, statement_origin);
    }
    for (const auto& item : input.sensitivities) {
      output.sensitivities.push_back(sensitivity(
          item, child_scope, statement_origin));
    }
    output.assertion_message = input.assertion_message;
    output.assertion_severity = assertion_severity(input.assertion_severity);
    output.assertion_has_pass_action = input.assertion_has_pass_action;
    output.assertion_has_failure_action = input.assertion_has_failure_action;
    output.output_text = input.output_text;
    output.output_newline = input.output_newline;
    output.output_postponed = input.output_postponed;
    if (input.output_format) {
      output.output_format = output_format(*input.output_format);
    }
    output.output_prefix = input.output_prefix;
    output.output_suffix = input.output_suffix;
    output.output_suppress_leading_zero = input.output_suppress_leading_zero;
    output.output_minimum_width = input.output_minimum_width;
    output.output_left_justify = input.output_left_justify;
    output.output_zero_pad = input.output_zero_pad;
    output.output_monitor = input.output_monitor;
    output.monitor_enabled = input.monitor_enabled;
    output.file_handle = expression(
        input.file_handle, child_scope, statement_origin);
    output.memory_hex = input.memory_hex;
    output.memory_write = input.memory_write;
    for (const auto& item : input.output_values) {
      if (const auto value = expression(
              item.value, child_scope, statement_origin)) {
        output.output_values.push_back({
            *value,
            output_format(item.format),
            item.prefix,
            item.suppress_leading_zero,
            item.minimum_width,
            item.left_justify,
            item.zero_pad});
      }
    }
    output.output_trailing_text = input.output_trailing_text;
    const auto transfer_type = !input.target.nominal_type.empty()
        ? input.target.nominal_type
        : input.value.nominal_type;
    if (!transfer_type.empty()
        && (input.kind == frontend::StatementKind::Assignment
            || input.kind == frontend::StatementKind::Return)) {
      output.class_handle_transfer = true;
      output.class_handle_type = transfer_type;
    }
    for (const auto& child : input.statements) {
      output.statements.push_back(statement(
          child, child_scope, statement_origin));
    }
    for (const auto& child : input.else_statements) {
      output.else_statements.push_back(statement(
          child, child_scope, statement_origin));
    }
    output.case_match = case_match_kind(input.case_match_kind);
    output.case_qualifier = case_qualifier(input.case_qualifier);
    for (const auto& alternative : input.case_alternatives) {
      sv::CaseAlternative converted;
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
      output.case_alternatives.push_back(std::move(converted));
    }
    hir_.mutable_statements().push_back(std::move(output));
    return id;
  }

  [[nodiscard]] sv::ProcessKind process_kind(
      const frontend::ProcessKind kind) const noexcept {
    switch (kind) {
      case frontend::ProcessKind::VerilogAlways:
        return sv::ProcessKind::always;
      case frontend::ProcessKind::SystemVerilogAlwaysFF:
        return sv::ProcessKind::always_ff;
      case frontend::ProcessKind::SystemVerilogAlwaysComb:
        return sv::ProcessKind::always_comb;
      case frontend::ProcessKind::SystemVerilogAlwaysLatch:
        return sv::ProcessKind::always_latch;
      case frontend::ProcessKind::Initial:
        return sv::ProcessKind::initial;
      case frontend::ProcessKind::Final:
        return sv::ProcessKind::final;
      case frontend::ProcessKind::VhdlProcess:
        return sv::ProcessKind::always;
    }
    return sv::ProcessKind::always;
  }

  [[nodiscard]] semantic::DeclarationId existing_variable(
      const frontend::VariableDeclaration& input,
      const semantic::ScopeId scope) {
    const auto found = find_declaration(
        scope, sv::DeclarationForm::variable, input.name, input.span);
    if (found == nullptr) {
      throw std::logic_error{"missing SystemVerilog HIR process variable"};
    }
    return found->id;
  }

  void fill_process(
      const frontend::Process& input,
      const semantic::ProcessId id) {
    const auto& identity = model_.process_identities()[id.value()];
    sv::Process output;
    output.id = id;
    output.scope = identity.scope;
    output.kind = process_kind(input.kind);
    output.name = identity.name;
    output.source = identity.source;
    output.origin = identity.origin;
    for (const auto& declaration : input.variables) {
      static_cast<void>(existing_variable(declaration, identity.scope));
      const auto* hir_declaration = find_declaration(
          identity.scope, sv::DeclarationForm::variable,
          declaration.name, declaration.span);
      fill_type_expressions(
          declaration.type, declaration.span, identity.scope,
          hir_declaration->origin);
      if (declaration.initializer) {
        static_cast<void>(expression(
            *declaration.initializer, identity.scope,
            hir_declaration->origin));
      }
    }
    for (const auto& parameter : input.constants) {
      fill_parameter(parameter, identity.scope);
    }
    for (const auto& type : input.type_aliases) {
      fill_type_declaration(type, identity.scope);
    }
    for (const auto& function : input.functions) {
      fill_function(function, identity.scope);
    }
    for (const auto& declaration : hir_.declarations()) {
      if (declaration.scope == identity.scope) {
        output.declarations.push_back(declaration.id);
      }
    }
    for (const auto& item : input.sensitivities) {
      output.sensitivities.push_back(sensitivity(
          item, identity.scope, identity.origin));
    }
    for (const auto& item : input.statements) {
      output.statements.push_back(statement(
          item, identity.scope, identity.origin));
    }
    hir_.mutable_processes().push_back(std::move(output));
  }

  void fill_parameter(
      const frontend::ParameterDeclaration& input,
      const semantic::ScopeId scope) {
    const auto form = input.kind == frontend::ParameterKind::Type
        ? sv::DeclarationForm::type_parameter
        : (input.local ? sv::DeclarationForm::local_parameter
                       : sv::DeclarationForm::parameter);
    auto* declaration = find_declaration(scope, form, input.name, input.span);
    if (declaration == nullptr) {
      return;
    }
    const auto declaration_origin = declaration->origin;
    fill_type_expressions(
        input.type, input.span, scope, declaration_origin);
    if (input.default_type) {
      fill_type_expressions(
          *input.default_type, input.span, scope, declaration_origin);
    }
    if (input.kind != frontend::ParameterKind::Type) {
      static_cast<void>(expression(
          input.default_value, scope, declaration_origin));
    }
  }

  template <typename Input>
  void fill_object(
      const Input& input,
      const semantic::ScopeId scope,
      const sv::DeclarationForm form,
      const std::optional<frontend::Expression>& initializer) {
    auto* declaration = find_declaration(scope, form, input.name, input.span);
    if (declaration == nullptr) {
      return;
    }
    const auto id = declaration->id;
    const auto declaration_origin = declaration->origin;
    fill_type_expressions(
        input.type, input.span, scope, declaration_origin);
    if (initializer) {
      static_cast<void>(expression(
          *initializer, scope, declaration_origin));
    }
    if constexpr (std::same_as<Input, frontend::SignalDeclaration>) {
      if (input.net_delay) {
        const auto converted = delay(
            *input.net_delay, scope, declaration_origin);
        const auto refreshed = std::ranges::find_if(
            hir_.mutable_declarations(),
            [&](const sv::Declaration& candidate) {
              return candidate.id == id;
            });
        refreshed->delay = converted;
      }
    }
  }

  void fill_type_declaration(
      const frontend::TypeAliasDeclaration& input,
      const semantic::ScopeId scope) {
    auto* declaration = find_declaration(
        scope, sv::DeclarationForm::typedef_declaration,
        input.name, input.span);
    if (declaration == nullptr) {
      return;
    }
    const auto declaration_origin = declaration->origin;
    fill_type_expressions(
        input.type, input.span, scope, declaration_origin);
    for (const auto& member : input.type.packed_members) {
      if (member.initializer) {
        static_cast<void>(expression(
            *member.initializer, scope, declaration_origin));
      }
    }
    for (const auto& literal : input.enum_literals) {
      auto* literal_declaration = find_declaration(
          scope, sv::DeclarationForm::enumeration_literal,
          literal.name, literal.span);
      if (literal_declaration != nullptr) {
        static_cast<void>(expression(
            literal.value, scope, literal_declaration->origin));
      }
    }
  }

  void fill_function(
      const frontend::FunctionDeclaration& input,
      const semantic::ScopeId scope) {
    auto* declaration = find_declaration(
        scope, sv::DeclarationForm::function, input.name, input.span);
    if (declaration == nullptr || !declaration->nested_scope) {
      return;
    }
    const auto id = declaration->id;
    const auto callable_scope = *declaration->nested_scope;
    const auto callable_origin = declaration->origin;
    fill_type_expressions(
        input.return_type, input.span, scope, callable_origin);
    for (const auto& parameter : input.constants) {
      fill_parameter(parameter, callable_scope);
    }
    for (const auto& type : input.type_aliases) {
      fill_type_declaration(type, callable_scope);
    }
    for (const auto& argument : input.arguments) {
      fill_object(
          argument, callable_scope, sv::DeclarationForm::port,
          argument.default_value);
    }
    for (const auto& variable : input.variables) {
      fill_object(
          variable, callable_scope, sv::DeclarationForm::variable,
          variable.initializer);
    }
    for (const auto& nested : input.functions) {
      fill_function(nested, callable_scope);
    }
    std::vector<semantic::StatementId> statements;
    for (const auto& item : input.statements) {
      statements.push_back(statement(item, callable_scope, callable_origin));
    }
    const auto refreshed = std::ranges::find_if(
        hir_.mutable_declarations(), [&](const sv::Declaration& candidate) {
          return candidate.id == id;
        });
    refreshed->statements = std::move(statements);
  }

  void fill_task(
      const frontend::TaskDeclaration& input,
      const semantic::ScopeId scope) {
    auto* declaration = find_declaration(
        scope, sv::DeclarationForm::task, input.name, input.span);
    if (declaration == nullptr || !declaration->nested_scope) {
      return;
    }
    const auto id = declaration->id;
    const auto callable_scope = *declaration->nested_scope;
    const auto callable_origin = declaration->origin;
    for (const auto& argument : input.arguments) {
      fill_object(
          argument, callable_scope, sv::DeclarationForm::port,
          argument.default_value);
    }
    for (const auto& variable : input.variables) {
      fill_object(
          variable, callable_scope, sv::DeclarationForm::variable,
          variable.initializer);
    }
    std::vector<semantic::StatementId> statements;
    for (const auto& item : input.statements) {
      statements.push_back(statement(item, callable_scope, callable_origin));
    }
    const auto refreshed = std::ranges::find_if(
        hir_.mutable_declarations(), [&](const sv::Declaration& candidate) {
          return candidate.id == id;
        });
    refreshed->statements = std::move(statements);
  }

  void fill_generate_body(
      const frontend::GenerateBody& input,
      sv::GenerateRegion& output) {
    for (const auto& parameter : input.constants) {
      fill_parameter(parameter, output.scope);
    }
    for (const auto& type : input.type_aliases) {
      fill_type_declaration(type, output.scope);
    }
    for (const auto& signal : input.signals) {
      fill_object(
          signal, output.scope,
          signal.is_port ? sv::DeclarationForm::port
                         : sv::DeclarationForm::net,
          signal.default_value);
    }
    for (const auto& variable : input.variables) {
      fill_object(
          variable, output.scope, sv::DeclarationForm::variable,
          variable.initializer);
    }
    for (const auto& function : input.functions) {
      fill_function(function, output.scope);
    }
    for (const auto& task : input.tasks) {
      fill_task(task, output.scope);
    }
    for (std::size_t index = 0;
         index < input.processes.size() && index < output.processes.size();
         ++index) {
      fill_process(input.processes[index], output.processes[index]);
    }
    for (const auto& item : input.concurrent_statements) {
      output.concurrent_statements.push_back(statement(
          item, output.scope, output.origin));
    }
    for (const auto& nested_input : input.generate_regions) {
      const auto nested_source = source(nested_input.span);
      const auto nested = std::ranges::find_if(
          output.nested, [&](const sv::GenerateRegion& candidate) {
            return candidate.source == nested_source;
          });
      if (nested != output.nested.end()) {
        fill_generate(nested_input, *nested);
      }
    }
  }

  void fill_generate(
      const frontend::GenerateRegion& input,
      sv::GenerateRegion& output) {
    static_cast<void>(expression(
        input.initial, output.scope, output.origin));
    static_cast<void>(expression(
        input.condition, output.scope, output.origin));
    static_cast<void>(expression(
        input.iteration, output.scope, output.origin));
    fill_generate_body(input.then_body, output);
    std::size_t nested_index = input.then_body.generate_regions.size();
    if (!input.else_scope.empty()
        || !input.else_body.constants.empty()
        || !input.else_body.type_aliases.empty()
        || !input.else_body.signals.empty()
        || !input.else_body.variables.empty()
        || !input.else_body.functions.empty()
        || !input.else_body.tasks.empty()
        || !input.else_body.processes.empty()
        || !input.else_body.concurrent_statements.empty()
        || !input.else_body.instances.empty()
        || !input.else_body.generate_regions.empty()) {
      if (nested_index < output.nested.size()) {
        fill_generate_body(input.else_body, output.nested[nested_index]);
      }
      ++nested_index;
    }
    for (const auto& alternative : input.alternatives) {
      sv::GenerateAlternative converted;
      converted.source = source(alternative.span);
      converted.label = alternative.scope;
      converted.is_default = alternative.is_default;
      for (const auto& choice : alternative.choices) {
        const auto left = expression(
            choice.left, output.scope, output.origin);
        if (!left) {
          continue;
        }
        sv::GenerateChoice converted_choice;
        converted_choice.left = *left;
        converted_choice.descending = choice.descending;
        converted_choice.source = source(choice.span);
        if (choice.right) {
          converted_choice.right = expression(
              *choice.right, output.scope, output.origin);
        }
        converted.choices.push_back(std::move(converted_choice));
      }
      if (nested_index < output.nested.size()) {
        converted.scope = output.nested[nested_index].scope;
        fill_generate_body(
            alternative.body, output.nested[nested_index]);
      }
      output.alternatives.push_back(std::move(converted));
      ++nested_index;
    }
  }

  void fill_unit(
      const frontend::DesignUnit& input,
      sv::Unit& output) {
    for (const auto& parameter : input.parameters) {
      fill_parameter(parameter, output.scope);
    }
    for (const auto& type : input.type_aliases) {
      fill_type_declaration(type, output.scope);
    }
    for (const auto& port : input.ports) {
      fill_object(
          port, output.scope, sv::DeclarationForm::port,
          port.default_value);
    }
    for (const auto& signal : input.signals) {
      fill_object(
          signal, output.scope,
          signal.is_port ? sv::DeclarationForm::port
                         : sv::DeclarationForm::net,
          signal.default_value);
    }
    for (const auto& variable : input.variables) {
      fill_object(
          variable, output.scope, sv::DeclarationForm::variable,
          variable.initializer);
    }
    for (const auto& function : input.functions) {
      fill_function(function, output.scope);
    }
    for (const auto& task : input.tasks) {
      fill_task(task, output.scope);
    }
    for (std::size_t index = 0;
         index < input.processes.size() && index < output.processes.size();
         ++index) {
      fill_process(input.processes[index], output.processes[index]);
    }
    for (const auto& item : input.concurrent_statements) {
      output.concurrent_statements.push_back(statement(
          item, output.scope, output.origin));
    }
    for (std::size_t index = 0;
         index < input.generate_regions.size()
         && index < output.generates.size();
         ++index) {
      fill_generate(input.generate_regions[index], output.generates[index]);
    }
  }

  void resolve_expression_names() {
    for (auto& item : hir_.mutable_expressions()) {
      if (!item.referenced_name) {
        continue;
      }
      item.referenced_name->selected.reset();
      item.referenced_name->overloads.clear();
      auto visible_scope = std::optional<semantic::ScopeId>{item.scope};
      while (visible_scope) {
        for (const auto& declaration : hir_.declarations()) {
          if (declaration.scope == *visible_scope
              && declaration.name == item.referenced_name->spelling) {
            item.referenced_name->overloads.push_back(declaration.id);
          }
        }
        if (!item.referenced_name->overloads.empty()) {
          break;
        }
        visible_scope = parent_scope(*visible_scope);
      }
      if (item.referenced_name->overloads.size() == 1) {
        item.referenced_name->selected =
            item.referenced_name->overloads.front();
      }
    }
  }

  semantic::Model& model_;
  sv::Hir& hir_;
  std::set<std::uint32_t> claimed_expressions_;
};

} // namespace

void complete_systemverilog_executable_hir(
    const frontend::ParsedDesign& parsed,
    semantic::Model& semantics,
    semantic::sv::Hir& hir) {
  SystemVerilogExecutableBuilder builder{semantics, hir};
  builder.add_design(parsed);
}

} // namespace fsim::app::application_detail
