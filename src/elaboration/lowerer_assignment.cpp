// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;



    std::optional<RegisterId> Lowerer::lower_condition(
        const Expression& expression,
        std::string diagnostic_code,
        std::string_view construct) {
        const auto expression_width =
            infer_width(expression).value_or(std::size_t{1});
        const auto source =
            lower_expression(expression, expression_width);
        if (!source) {
            return std::nullopt;
        }
        if (language_ == frontend::Language::Vhdl2008) {
            if (register_width(*source) != 1
                || register_domain(*source)
                    != frontend::ValueDomain::Boolean) {
                report(
                    std::move(diagnostic_code),
                    "a VHDL " + std::string{construct}
                        + " condition must have type boolean",
                    expression.span);
                return std::nullopt;
            }
            return source;
        }

        // SystemVerilog conditionals apply logical truth conversion to the
        // complete expression. Reusing logical negation twice preserves 0,
        // 1, and unknown truth while normalizing any packed width to a
        // scalar. Branching subsequently treats X/Z as false, as required
        // for procedural conditions.
        const auto inverted =
            allocate_register(1, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(LogicalNot{inverted, *source});
        const auto normalized =
            allocate_register(1, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(
            LogicalNot{normalized, inverted});
        return normalized;
    }



    void Lowerer::lower_assert(const Statement& statement) {
        const auto condition = lower_condition(
            statement.condition, "FSIM-ELAB-051", "assertion");
        if (!condition) {
            return;
        }
        if (language_
            == frontend::Language::SystemVerilog2017) {
            const auto branch_index =
                static_cast<InstructionIndex>(
                    process_.operations.size());
            process_.operations.emplace_back(
                Branch{
                    *condition,
                    0,
                    0,
                    UnknownBranchPolicy::when_false});
            const auto pass_start =
                static_cast<InstructionIndex>(
                    process_.operations.size());
            lower_statements(statement.statements);
            const auto jump_index =
                static_cast<InstructionIndex>(
                    process_.operations.size());
            process_.operations.emplace_back(Jump{0});
            const auto failure_start =
                static_cast<InstructionIndex>(
                    process_.operations.size());
            if (statement.assertion_has_failure_action) {
                lower_statements(statement.else_statements);
            } else {
                process_.operations.emplace_back(
                    runtime::simir::Report{
                        statement.assertion_message.empty()
                            ? "assertion failed"
                            : statement.assertion_message,
                        AssertionSeverity::error,
                        SourceLocation{
                            statement.span.source_name,
                            static_cast<std::uint32_t>(
                                statement.span.begin.line),
                            static_cast<std::uint32_t>(
                                statement.span.begin.column)}});
            }
            const auto end =
                static_cast<InstructionIndex>(
                    process_.operations.size());
            process_.operations[branch_index] =
                Branch{
                    *condition,
                    pass_start,
                    failure_start,
                    UnknownBranchPolicy::when_false};
            process_.operations[jump_index] = Jump{end};
            return;
        }
        AssertionSeverity severity = AssertionSeverity::error;
        switch (statement.assertion_severity) {
        case frontend::AssertionSeverity::Note:
            severity = AssertionSeverity::note;
            break;
        case frontend::AssertionSeverity::Warning:
            severity = AssertionSeverity::warning;
            break;
        case frontend::AssertionSeverity::Error:
            severity = AssertionSeverity::error;
            break;
        case frontend::AssertionSeverity::Failure:
            severity = AssertionSeverity::failure;
            break;
        }
        process_.operations.emplace_back(Assert{
            *condition,
            statement.assertion_message,
            severity,
            SourceLocation{
                statement.span.source_name,
                static_cast<std::uint32_t>(statement.span.begin.line),
                static_cast<std::uint32_t>(statement.span.begin.column)}});
    }



    [[nodiscard]] const frontend::Type*
    Lowerer::vhdl_array_attribute_prefix_type(
        const Expression& expression) const {
        if (language_ != frontend::Language::Vhdl2008
            || expression.kind != ExpressionKind::Call
            || expression.operands.empty()
            || expression.operands.front().kind
                != ExpressionKind::Identifier) {
            return nullptr;
        }
        const auto& prefix =
            expression.operands.front().text;
        if (const auto* object = object_type(prefix);
            object != nullptr) {
            return object;
        }
        return visible_type_mark(prefix);
    }



    [[nodiscard]] bool Lowerer::is_vhdl_array_like(
        const frontend::Type& type) {
        if (type.vhdl_array) {
            return true;
        }
        const auto separator =
            type.spelling.find_last_of('.');
        const auto name = type.spelling.substr(
            separator == std::string::npos
                ? 0
                : separator + 1);
        return name == "bit_vector"
            || name == "std_logic_vector"
            || name == "std_ulogic_vector"
            || name == "signed"
            || name == "unsigned";
    }



    std::optional<frontend::PackedRange>
    Lowerer::vhdl_array_attribute_range(
        const Expression& expression,
        const bool report_errors) {
        const auto* type =
            vhdl_array_attribute_prefix_type(expression);
        if (type == nullptr || !is_vhdl_array_like(*type)) {
            if (report_errors) {
                report(
                    "FSIM-ELAB-VHARRAYATTR-001",
                    expression.text
                        + " requires a visible bounded array object, "
                          "type, or subtype mark",
                    expression.span);
            }
            return std::nullopt;
        }
        if (expression.operands.size() < 1
            || expression.operands.size() > 2) {
            if (report_errors) {
                report(
                    "FSIM-ELAB-VHARRAYATTR-001",
                    expression.text
                        + " accepts at most one dimension argument",
                    expression.span);
            }
            return std::nullopt;
        }
        if (expression.operands.size() == 2) {
            const auto dimension =
                constant_index(expression.operands[1]);
            if (!dimension || *dimension != 1) {
                if (report_errors) {
                    report(
                        "FSIM-ELAB-VHARRAYATTR-002",
                        expression.text
                            + " supports only the locally static "
                              "dimension 1",
                        expression.operands[1].span);
                }
                return std::nullopt;
            }
        }
        if (!type->packed_range
            || type->packed_range->width() == 0) {
            if (report_errors) {
                report(
                    "FSIM-ELAB-VHARRAYATTR-001",
                    expression.text
                        + " requires a concrete non-null array "
                          "constraint",
                    expression.operands.front().span);
            }
            return std::nullopt;
        }
        return type->packed_range;
    }



    std::optional<std::int64_t>
    Lowerer::static_integer_value(const Expression& expression) {
        if (language_ != frontend::Language::Vhdl2008) {
            return constant_index(expression);
        }
        auto folded = expression;
        const auto fold_attributes =
            [&](const auto& self,
                Expression& candidate) -> bool {
              for (auto& operand : candidate.operands) {
                  if (!self(self, operand)) {
                      return false;
                  }
              }
              if (candidate.kind != ExpressionKind::Call
                  || (candidate.text != "'left"
                      && candidate.text != "'right"
                      && candidate.text != "'low"
                      && candidate.text != "'high"
                      && candidate.text != "'length"
                      && candidate.text != "'ascending")
                  || vhdl_array_attribute_prefix_type(candidate)
                      == nullptr) {
                  return true;
              }
              const auto span = candidate.span;
              const auto range =
                  vhdl_array_attribute_range(candidate, false);
              if (!range) {
                  return false;
              }
              std::int64_t value = 0;
              if (candidate.text == "'left") {
                  value = range->left;
              } else if (candidate.text == "'right") {
                  value = range->right;
              } else if (candidate.text == "'low") {
                  value = std::min(
                      range->left, range->right);
              } else if (candidate.text == "'high") {
                  value = std::max(
                      range->left, range->right);
              } else if (candidate.text == "'ascending") {
                  value = range->descending ? 0 : 1;
              } else {
                  const auto width = range->width();
                  if (width
                      > static_cast<std::uint64_t>(
                          std::numeric_limits<
                              std::int64_t>::max())) {
                      return false;
                  }
                  value =
                      static_cast<std::int64_t>(width);
              }
              candidate = constant_expression(
                  value,
                  span,
                  frontend::ValueDomain::Integer,
                  frontend::Language::Vhdl2008);
              return true;
            };
        if (!fold_attributes(
                fold_attributes, folded)) {
            return std::nullopt;
        }
        return constant_index(folded);
    }



    std::optional<DynamicIndex>
    Lowerer::lower_dynamic_index(
        const Expression& source,
        const Expression& index,
        const std::size_t source_width,
        const std::uint32_t base_offset,
        const frontend::SourceSpan& span) {
        const auto range =
            expression_range(source, source_width);
        if (!range
            || range->left
                < std::numeric_limits<std::int32_t>::min()
            || range->left
                > std::numeric_limits<std::int32_t>::max()
            || range->right
                < std::numeric_limits<std::int32_t>::min()
            || range->right
                > std::numeric_limits<std::int32_t>::max()
            || range->width()
                > static_cast<std::uint64_t>(
                    std::numeric_limits<std::uint32_t>::max()
                    - base_offset)
                    + 1U) {
            report(
                "FSIM-ELAB-DYNINDEX-001",
                "dynamic packed selection requires a concrete "
                "one-dimensional range representable by signed 32-bit "
                "indices and normalized offsets",
                span);
            return std::nullopt;
        }
        if (language_ == frontend::Language::Vhdl2008
            && !is_integer_expression(index)) {
            report(
                "FSIM-ELAB-DYNINDEX-002",
                "a dynamic VHDL array index requires an integer-family "
                "expression",
                index.span);
            return std::nullopt;
        }
        const auto lowered = lower_expression(index, 32);
        if (!lowered || register_width(*lowered) != 32) {
            report(
                "FSIM-ELAB-DYNINDEX-002",
                "a dynamic packed index must lower to the signed 32-bit "
                "runtime representation",
                index.span);
            return std::nullopt;
        }
        return DynamicIndex{
            *lowered,
            range->left,
            range->right,
            base_offset};
    }



    std::optional<Lowerer::ConstantSliceSelection>
    Lowerer::constant_slice_selection(
        const Expression& expression,
        const std::size_t source_width) {
        if (expression.kind != ExpressionKind::Slice
            || expression.operands.size() != 3) {
            return std::nullopt;
        }
        const auto& source = expression.operands[0];
        const auto range =
            expression_range(source, source_width);
        if (!range) {
            return std::nullopt;
        }

        std::int64_t left = 0;
        std::int64_t right = 0;
        std::uint64_t width = 0;
        if (expression.text == "+:"
            || expression.text == "-:") {
            const auto base =
                static_integer_value(expression.operands[1]);
            const auto selected_width =
                static_integer_value(expression.operands[2]);
            if (!base || !selected_width
                || *selected_width <= 0) {
                return std::nullopt;
            }
            const auto distance = *selected_width - 1;
            std::int64_t lower = 0;
            std::int64_t upper = 0;
            if (expression.text == "+:") {
                if (*base
                    > std::numeric_limits<std::int64_t>::max()
                          - distance) {
                    return std::nullopt;
                }
                lower = *base;
                upper = *base + distance;
            } else {
                if (*base
                    < std::numeric_limits<std::int64_t>::min()
                          + distance) {
                    return std::nullopt;
                }
                lower = *base - distance;
                upper = *base;
            }
            if (range->left >= range->right) {
                left = upper;
                right = lower;
            } else {
                left = lower;
                right = upper;
            }
            width = static_cast<std::uint64_t>(*selected_width);
        } else {
            const auto parsed_left =
                static_integer_value(expression.operands[1]);
            const auto parsed_right =
                static_integer_value(expression.operands[2]);
            if (!parsed_left || !parsed_right) {
                return std::nullopt;
            }
            left = *parsed_left;
            right = *parsed_right;
            const bool selected_descending = left >= right;
            if (left != right
                && selected_descending
                    != (range->left >= range->right)) {
                return std::nullopt;
            }
            width = index_distance(left, right) + 1;
        }
        const auto offset =
            select_offset(source, right, source_width);
        const auto left_offset =
            select_offset(source, left, source_width);
        if (!offset || !left_offset
            || width == 0
            || width
                > std::numeric_limits<std::size_t>::max()) {
            return std::nullopt;
        }
        return ConstantSliceSelection{
            *offset, static_cast<std::size_t>(width)};
    }



    void Lowerer::lower_assignment(const Statement& statement) {
        const Expression* base = &statement.target;
        std::uint32_t selected_offset = 0;
        bool has_selected_offset = false;
        std::optional<std::size_t> selected_width;
        std::optional<frontend::ValueDomain> selected_domain;
        std::optional<DynamicIndex> dynamic_selection;
        if (statement.target.kind == ExpressionKind::Index
            && statement.target.operands.size() == 2) {
            base = &statement.target.operands[0];
        } else if (
            statement.target.kind == ExpressionKind::Slice
            && statement.target.operands.size() == 3) {
            base = &statement.target.operands[0];
        } else if (statement.target.kind != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-031",
                "an assignment target must be a packed object, bit-select, "
                "or constant part-select",
                statement.target.span);
            return;
        }
        if (base->kind != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-031",
                "nested or aggregate selected assignment targets are not "
                "executable yet",
                statement.target.span);
            return;
        }

        auto target_name = base->text;
        const auto container_local =
            container_locals_.find(target_name);
        const auto container_object =
            container_objects_.find(target_name);
        if (container_local != container_locals_.end()
            || container_object != container_objects_.end()) {
            if (statement.assignment_kind
                    != AssignmentKind::Blocking
                || statement.procedural_assignment_control
                    != frontend::ProceduralAssignmentControl::None) {
                report(
                    "FSIM-ELAB-SVCONTAINER-009",
                    "container assignments must be blocking and time-free",
                    statement.span);
                return;
            }
            const auto* source_type = object_type(target_name);
            if (source_type == nullptr) {
                return;
            }
            const auto runtime_type =
                container_type(*source_type, statement.target.span);
            const auto element_width = source_type->width();
            if (!runtime_type || !element_width) {
                return;
            }
            ContainerRegisterId target{};
            if (container_local != container_locals_.end()) {
                target = container_local->second;
            } else {
                target = allocate_container_register(*runtime_type);
                process_.operations.emplace_back(
                    ReadContainerObject{
                        target, container_object->second});
            }
            if (statement.target.kind
                == ExpressionKind::Identifier) {
                const bool is_new =
                    statement.value.kind
                            == ExpressionKind::Index
                    && statement.value.operands.size() == 2
                    && statement.value.operands.front().kind
                            == ExpressionKind::Identifier
                    && statement.value.operands.front().text
                            == "new";
                if (is_new) {
                    if (runtime_type->associative
                        || runtime_type->fixed) {
                        report(
                            "FSIM-ELAB-SVCONTAINER-014",
                            runtime_type->fixed
                                ? "new[size] cannot resize a static array"
                                : "new[size] cannot resize an associative "
                                  "array",
                            statement.value.span);
                        return;
                    }
                    const auto size_width =
                        infer_width(statement.value.operands[1])
                            .value_or(std::size_t{32});
                    const auto size = lower_expression(
                        statement.value.operands[1], size_width);
                    if (!size) {
                        return;
                    }
                    process_.operations.emplace_back(
                        ResizeContainer{target, *size});
                } else {
                    const auto value =
                        lower_container_expression(statement.value);
                    if (!value) {
                        report(
                            "FSIM-ELAB-SVCONTAINER-010",
                            "whole-container assignment requires new[size] "
                            "or a compatible container value",
                            statement.value.span);
                        return;
                    }
                    process_.operations.emplace_back(
                        CopyContainerRegister{target, *value});
                }
            } else if (
                statement.target.kind == ExpressionKind::Index
                && statement.target.operands.size() == 2) {
                const auto index_width =
                    runtime_type->associative
                        ? static_cast<std::size_t>(
                              runtime_type->index_width)
                        : runtime_type->fixed
                              ? std::size_t{32}
                              : infer_width(
                                    statement.target.operands[1])
                                    .value_or(std::size_t{32});
                const auto* index_type =
                    runtime_type->associative
                        ? source_type->systemverilog_container
                              ->associative_index_type.get()
                        : nullptr;
                const auto index = lower_expression(
                    statement.target.operands[1],
                    index_width,
                    index_type);
                const auto value = lower_expression(
                    statement.value, *element_width, source_type);
                if (!index || !value) {
                    return;
                }
                process_.operations.emplace_back(
                    ContainerWrite{
                        target,
                        *index,
                        *value,
                        runtime_type->associative
                            ? runtime_type->signed_indices
                            : runtime_type->fixed
                                  || is_signed_expression(
                                      statement.target.operands[1])});
            } else {
                report(
                    "FSIM-ELAB-SVCONTAINER-011",
                    "container targets support only whole-value or "
                    "element-index blocking assignment",
                    statement.target.span);
                return;
            }
            if (container_object != container_objects_.end()) {
                process_.operations.emplace_back(
                    WriteContainerObject{
                        container_object->second, target});
            }
            return;
        }
        const auto string_local =
            string_locals_.find(target_name);
        const auto string_object =
            string_objects_.find(target_name);
        if (string_local != string_locals_.end()
            || string_object != string_objects_.end()) {
            if (statement.assignment_kind
                    != AssignmentKind::Blocking
                || statement.procedural_assignment_control
                    != frontend::ProceduralAssignmentControl::None) {
                report(
                    "FSIM-ELAB-SVSTRING-013",
                    "string assignments must be blocking and time-free",
                    statement.span);
                return;
            }
            if (statement.target.kind
                == ExpressionKind::Identifier) {
                const auto value =
                    lower_string_expression(statement.value);
                if (!value) {
                    report(
                        "FSIM-ELAB-SVSTRING-014",
                        "string assignment requires a string value",
                        statement.value.span);
                    return;
                }
                if (string_local != string_locals_.end()) {
                    process_.operations.emplace_back(
                        CopyStringRegister{
                            string_local->second, *value});
                } else {
                    process_.operations.emplace_back(
                        WriteStringObject{
                            string_object->second, *value});
                }
                return;
            }
            if (statement.target.kind != ExpressionKind::Index
                || statement.target.operands.size() != 2) {
                report(
                    "FSIM-ELAB-SVSTRING-015",
                    "string targets support only whole-value or byte-index "
                    "blocking assignment",
                    statement.target.span);
                return;
            }
            StringRegisterId target{};
            if (string_local != string_locals_.end()) {
                target = string_local->second;
            } else {
                target = allocate_string_register();
                process_.operations.emplace_back(
                    ReadStringObject{
                        target, string_object->second});
            }
            const auto index_width =
                infer_width(statement.target.operands[1])
                    .value_or(std::size_t{32});
            const auto index =
                lower_expression(
                    statement.target.operands[1],
                    index_width);
            std::optional<RegisterId> byte;
            if (statement.value.kind
                    == ExpressionKind::StringLiteral
                && statement.value.decoded_string
                && statement.value.decoded_string->size() == 1) {
                byte = allocate_register(
                    8, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(
                    LoadConstant{
                        *byte,
                        unsigned_value(
                            static_cast<unsigned char>(
                                statement.value.decoded_string->front()),
                            8)});
            } else {
                byte = lower_expression(statement.value, 8);
            }
            if (!index || !byte) {
                return;
            }
            process_.operations.emplace_back(
                StringReplaceByte{
                    target,
                    *index,
                    *byte,
                    is_signed_expression(
                        statement.target.operands[1])});
            if (string_object != string_objects_.end()) {
                process_.operations.emplace_back(
                    WriteStringObject{
                        string_object->second, target});
            }
            return;
        }
        if (!locals_.contains(target_name)
            && !signals_.contains(target_name)) {
            if (const auto selected =
                    packed_member_reference(target_name)) {
                const auto width = selected->member->width();
                if (!width || *width == 0
                    || *width
                        > std::numeric_limits<std::uint32_t>::max()
                    || selected->member->lsb_offset
                        > std::numeric_limits<std::uint32_t>::max()) {
                    report(
                        "FSIM-ELAB-SVSTRUCT-002",
                        "packed aggregate member '" + target_name
                            + "' has no executable layout",
                        statement.target.span);
                    return;
                }
                target_name = selected->base;
                selected_offset = static_cast<std::uint32_t>(
                    selected->member->lsb_offset);
                has_selected_offset = true;
                selected_width = static_cast<std::size_t>(*width);
                selected_domain = selected->member->domain;
            }
        }
        const auto local = locals_.find(target_name);
        const auto signal = signals_.find(target_name);
        if (local == locals_.end() && signal == signals_.end()) {
            report(
                "FSIM-ELAB-032",
                "unknown assignment target '" + target_name + "'",
                statement.target.span);
            return;
        }
        const auto whole_width =
            local != locals_.end()
                ? register_width(local->second)
                : design_.signal_info_[signal->second].width;
        const auto selection_source_width =
            selected_width.value_or(whole_width);

        if (statement.target.kind == ExpressionKind::Index) {
            const auto index =
                static_integer_value(
                    statement.target.operands[1]);
            const auto base_offset =
                static_cast<std::uint64_t>(
                    selected_offset);
            if (index) {
                const auto offset = select_offset(
                    *base, *index, selection_source_width);
                if (!offset
                    || base_offset + *offset
                        > std::numeric_limits<
                            std::uint32_t>::max()) {
                    report(
                        "FSIM-ELAB-068",
                        "an assignment bit-select requires an index "
                        "inside the target's declared packed range",
                        statement.target.span);
                    return;
                }
                selected_offset =
                    static_cast<std::uint32_t>(
                        base_offset + *offset);
                has_selected_offset = true;
            } else {
                dynamic_selection = lower_dynamic_index(
                    *base,
                    statement.target.operands[1],
                    selection_source_width,
                    selected_offset,
                    statement.target.span);
                if (!dynamic_selection) {
                    return;
                }
                selected_offset = 0;
                has_selected_offset = false;
            }
            selected_width = 1;
        } else if (statement.target.kind == ExpressionKind::Slice) {
            const auto selection =
                constant_slice_selection(
                    statement.target, selection_source_width);
            const auto base_offset =
                static_cast<std::uint64_t>(
                    selected_offset);
            if (!selection
                || base_offset + selection->offset
                    > std::numeric_limits<std::uint32_t>::max()
                || selection->width
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-068",
                    "an assignment part-select requires constant in-range "
                    "bounds, a positive indexed width, and a direction "
                    "compatible with the target's declared packed range",
                    statement.target.span);
                return;
            }
            selected_offset =
                static_cast<std::uint32_t>(
                    base_offset + selection->offset);
            has_selected_offset = true;
            selected_width = selection->width;
        }

        const auto target_width =
            selected_width.value_or(whole_width);
        const auto* contextual_target_type =
            has_selected_offset || dynamic_selection
                ? nullptr
                : object_type(target_name);
        const auto assignment_control =
            statement.procedural_assignment_control;
        const bool procedural_delay =
            assignment_control
            == frontend::ProceduralAssignmentControl::Delay;
        const bool procedural_event =
            assignment_control
            == frontend::ProceduralAssignmentControl::Event;
        if (assignment_control
                != frontend::ProceduralAssignmentControl::None
            && statement.assignment_kind != AssignmentKind::Blocking
            && statement.assignment_kind
                != AssignmentKind::NonBlocking) {
            report(
                "FSIM-ELAB-105",
                "procedural assignment control is attached to a "
                "nonprocedural assignment kind",
                statement.span);
            return;
        }
        if (assignment_control
                == frontend::ProceduralAssignmentControl::None
            && !statement.sensitivities.empty()) {
            report(
                "FSIM-ELAB-105",
                "procedural assignment event metadata has no event-control "
                "kind",
                statement.span);
            return;
        }
        if (procedural_delay
            && (!statement.delay
                || !statement.sensitivities.empty())) {
            report(
                "FSIM-ELAB-105",
                "procedural assignment delay control has invalid delay or "
                "sensitivity metadata",
                statement.span);
            return;
        }
        if (procedural_event
            && (statement.delay
                || statement.sensitivities.empty())) {
            report(
                "FSIM-ELAB-105",
                "procedural assignment event control has invalid delay or "
                "sensitivity metadata",
                statement.span);
            return;
        }
        if (procedural_event) {
            auto [signals, edges] =
                resolve_wait_sensitivities(statement);
            if (signals.empty()) {
                return;
            }
            emit_debug_point(
                DebugPointKind::wait, statement.span);
            process_.operations.emplace_back(
                WaitOn{std::move(signals), std::move(edges)});
        }
        if (local != locals_.end()) {
            if (statement.assignment_kind != AssignmentKind::Blocking) {
                report(
                    "FSIM-ELAB-056",
                    "local variable assignments require a blocking/variable "
                    "assignment",
                    statement.span);
                return;
            }
            auto value =
                lower_expression(
                    statement.value,
                    target_width,
                    contextual_target_type);
            if (!value) {
                return;
            }
            if (register_width(*value) != target_width
                && language_
                    != frontend::Language::Vhdl2008) {
                *value = resize_register(
                    *value,
                    target_width,
                    is_signed_expression(statement.value));
            }
            if (register_width(*value) != target_width) {
                report(
                    "FSIM-ELAB-057",
                    "local variable assignment width mismatch for '"
                        + target_name + "'",
                    statement.span);
                return;
            }
            const auto target_domain =
                selected_domain.value_or(
                    register_domain(local->second));
            if (is_two_state_domain(target_domain)
                && !is_two_state_domain(
                    register_domain(*value))) {
                report(
                    "FSIM-ELAB-058",
                    "assignment to two-state local variable '"
                        + target_name
                        + "' requires an explicit conversion",
                    statement.span);
                return;
            }
            if (language_ == frontend::Language::Vhdl2008
                && target_domain
                    == frontend::ValueDomain::Integer
                && !has_selected_offset
                && !dynamic_selection) {
                if (!is_integer_expression(statement.value)) {
                    report(
                        "FSIM-ELAB-INTEGER-004",
                        "assignment to VHDL integer local '"
                            + target_name
                            + "' requires an integer-family expression",
                        statement.span);
                    return;
                }
                const auto range =
                    local_integer_ranges_.contains(target_name)
                        ? local_integer_ranges_.at(target_name)
                        : std::optional<frontend::IntegerRange>{};
                if (!validate_static_integer_assignment(
                        statement.value, range, statement.span)) {
                    return;
                }
                emit_integer_check(*value, range);
            }
            if (language_ == frontend::Language::Vhdl2008
                && !has_selected_offset
                && !dynamic_selection
                && contextual_target_type != nullptr
                && !contextual_target_type
                        ->enumeration_literals.empty()) {
                if (!validate_static_enumeration_assignment(
                        statement.value,
                        *contextual_target_type,
                        statement.span)) {
                    return;
                }
                emit_enumeration_check(
                    *value, *contextual_target_type);
            }
            if (procedural_delay) {
                emit_debug_point(
                    DebugPointKind::wait, statement.span);
                process_.operations.emplace_back(
                    WaitFor{statement.delay->magnitude});
            }
            if (dynamic_selection) {
                process_.operations.emplace_back(DynamicInsert{
                    local->second,
                    local->second,
                    *value,
                    *dynamic_selection});
            } else if (has_selected_offset) {
                process_.operations.emplace_back(Insert{
                    local->second,
                    local->second,
                    *value,
                    selected_offset});
            } else {
                process_.operations.emplace_back(
                    CopyRegister{local->second, *value});
            }
            return;
        }
        if (statement.vhdl_unaffected) {
            return;
        }
        if (statement.vhdl_delay_mechanism
            && statement.vhdl_waveform.size() > 1) {
            std::vector<ProjectedWaveformElement> waveform;
            waveform.reserve(statement.vhdl_waveform.size());
            const auto* target_type =
                visible_type(target_name);
            const auto target_domain =
                selected_domain.value_or(
                    target_type != nullptr
                        ? target_type->domain
                        : design_.signal_info_[signal->second]
                              .source_domain);
            std::optional<runtime::SimulationTick> previous_delay;
            for (const auto& element : statement.vhdl_waveform) {
                const auto value =
                    lower_expression(
                        element.value,
                        target_width,
                        contextual_target_type);
                if (!value) {
                    return;
                }
                if (register_width(*value) != target_width) {
                    report(
                        "FSIM-ELAB-047",
                        "assignment width mismatch in VHDL waveform for '"
                            + target_name + "'",
                        element.span);
                    return;
                }
                if (is_two_state_domain(target_domain)
                    && !is_two_state_domain(
                        register_domain(*value))) {
                    report(
                        "FSIM-ELAB-050",
                        "assignment to two-state target '"
                            + target_name
                            + "' requires an explicit conversion from a "
                              "four-/nine-state expression",
                        element.span);
                    return;
                }
                if (language_ == frontend::Language::Vhdl2008
                    && target_domain
                        == frontend::ValueDomain::Integer
                    && !has_selected_offset
                    && !dynamic_selection) {
                    if (!is_integer_expression(element.value)) {
                        report(
                            "FSIM-ELAB-INTEGER-004",
                            "VHDL integer waveform for '"
                                + target_name
                                + "' requires integer-family expressions",
                            element.span);
                        return;
                    }
                    const auto& range =
                        target_type != nullptr
                            ? target_type->integer_range
                            : design_.signal_info_[signal->second]
                                  .integer_range;
                    if (!validate_static_integer_assignment(
                            element.value, range, element.span)) {
                        return;
                    }
                    emit_integer_check(*value, range);
                }
                if (language_
                        == frontend::Language::Vhdl2008
                    && !has_selected_offset
                    && !dynamic_selection
                    && contextual_target_type != nullptr
                    && !contextual_target_type
                            ->enumeration_literals.empty()) {
                    if (!validate_static_enumeration_assignment(
                            element.value,
                            *contextual_target_type,
                            element.span)) {
                        return;
                    }
                    emit_enumeration_check(
                        *value, *contextual_target_type);
                }
                const auto delay =
                    element.delay ? element.delay->magnitude : 0;
                if (previous_delay && delay <= *previous_delay) {
                    report(
                        "FSIM-ELAB-055",
                        "VHDL waveform-element delays must be strictly "
                        "ascending",
                        element.span);
                    return;
                }
                previous_delay = delay;
                waveform.push_back({*value, delay});
            }
            const auto mode =
                *statement.vhdl_delay_mechanism
                        == frontend::VhdlDelayMechanism::Transport
                    ? ProjectedDelayMode::transport
                    : ProjectedDelayMode::inertial;
            const auto first_delay = waveform.front().delay;
            const auto rejection =
                statement.vhdl_rejection_limit
                    ? statement.vhdl_rejection_limit->magnitude
                    : (mode == ProjectedDelayMode::inertial
                           ? first_delay
                           : 0);
            if (rejection > first_delay) {
                report(
                    "FSIM-ELAB-055",
                    "a VHDL rejection limit cannot exceed the first "
                    "waveform element delay",
                    statement.span);
                return;
            }
            if (dynamic_selection) {
                process_.operations.emplace_back(
                    WriteProjectedWaveformDynamicSlice{
                        signal->second,
                        std::move(waveform),
                        *dynamic_selection,
                        rejection,
                        mode});
            } else if (has_selected_offset) {
                process_.operations.emplace_back(
                    WriteProjectedWaveformSlice{
                        signal->second,
                        std::move(waveform),
                        selected_offset,
                        rejection,
                        mode});
            } else {
                process_.operations.emplace_back(
                    WriteProjectedWaveform{
                        signal->second,
                        std::move(waveform),
                        rejection,
                        mode});
            }
            return;
        }
        auto value = lower_expression(
            statement.value,
            target_width,
            contextual_target_type);
        if (!value) {
            return;
        }
        if (register_width(*value) != target_width
            && language_ != frontend::Language::Vhdl2008) {
            *value = resize_register(
                *value,
                target_width,
                is_signed_expression(statement.value));
        }
        if (register_width(*value) != target_width) {
            report(
                "FSIM-ELAB-047",
                "assignment width mismatch: target '"
                    + target_name + "' is "
                    + std::to_string(target_width)
                    + " bits but the expression is "
                    + std::to_string(register_width(*value)) + " bits",
                statement.span);
            return;
        }
        const auto* target_type =
            visible_type(target_name);
        const auto target_domain =
            selected_domain.value_or(
                target_type != nullptr
                    ? target_type->domain
                    : design_.signal_info_[signal->second]
                          .source_domain);
        if (is_two_state_domain(target_domain)
            && !is_two_state_domain(register_domain(*value))) {
            report(
                "FSIM-ELAB-050",
                "assignment to two-state target '"
                    + target_name
                    + "' requires an explicit conversion from a "
                      "four-/nine-state expression",
                statement.span);
            return;
        }
        if (language_ == frontend::Language::Vhdl2008
            && target_domain == frontend::ValueDomain::Integer
            && !has_selected_offset
            && !dynamic_selection) {
            if (!is_integer_expression(statement.value)) {
                report(
                    "FSIM-ELAB-INTEGER-004",
                    "assignment to VHDL integer signal '"
                        + target_name
                        + "' requires an integer-family expression",
                    statement.span);
                return;
            }
            const auto& range =
                target_type != nullptr
                    ? target_type->integer_range
                    : design_.signal_info_[signal->second]
                          .integer_range;
            if (!validate_static_integer_assignment(
                    statement.value, range, statement.span)) {
                return;
            }
            emit_integer_check(*value, range);
        }
        if (language_ == frontend::Language::Vhdl2008
            && !has_selected_offset
            && !dynamic_selection
            && contextual_target_type != nullptr
            && !contextual_target_type
                    ->enumeration_literals.empty()) {
            if (!validate_static_enumeration_assignment(
                    statement.value,
                    *contextual_target_type,
                    statement.span)) {
                return;
            }
            emit_enumeration_check(
                *value, *contextual_target_type);
        }
        if (procedural_delay
            && statement.assignment_kind == AssignmentKind::Blocking) {
            emit_debug_point(
                DebugPointKind::wait, statement.span);
            process_.operations.emplace_back(
                WaitFor{statement.delay->magnitude});
            if (dynamic_selection) {
                process_.operations.emplace_back(
                    WriteBlockingDynamicSlice{
                        signal->second,
                        *value,
                        *dynamic_selection});
            } else if (has_selected_offset) {
                process_.operations.emplace_back(
                    WriteBlockingSlice{
                        signal->second, *value, selected_offset});
            } else {
                process_.operations.emplace_back(
                    WriteBlocking{signal->second, *value});
            }
            return;
        }
        if (statement.vhdl_delay_mechanism) {
            const auto delay =
                statement.delay ? statement.delay->magnitude : 0;
            const auto mode =
                *statement.vhdl_delay_mechanism
                        == frontend::VhdlDelayMechanism::Transport
                    ? ProjectedDelayMode::transport
                    : ProjectedDelayMode::inertial;
            const auto rejection =
                statement.vhdl_rejection_limit
                    ? statement.vhdl_rejection_limit->magnitude
                    : (mode == ProjectedDelayMode::inertial
                           ? delay
                           : 0);
            if (rejection > delay) {
                report(
                    "FSIM-ELAB-055",
                    "a VHDL rejection limit cannot exceed the first "
                    "waveform element delay",
                    statement.span);
                return;
            }
            if (dynamic_selection) {
                process_.operations.emplace_back(
                    WriteProjectedDynamicSlice{
                        signal->second,
                        *value,
                        *dynamic_selection,
                        delay,
                        rejection,
                        mode});
            } else if (has_selected_offset) {
                process_.operations.emplace_back(
                    WriteProjectedSlice{
                        signal->second,
                        *value,
                        selected_offset,
                        delay,
                        rejection,
                        mode});
            } else {
                process_.operations.emplace_back(
                    WriteProjected{
                        signal->second,
                        *value,
                        delay,
                        rejection,
                        mode});
            }
        } else if (statement.delay) {
            if (statement.assignment_kind
                == AssignmentKind::Continuous) {
                const auto rise = statement.delay->magnitude;
                const auto fall =
                    statement.delay->additional_values.empty()
                        ? rise
                        : statement.delay->additional_values[0].magnitude;
                const auto turnoff =
                    statement.delay->additional_values.size() < 2
                        ? std::min(rise, fall)
                        : statement.delay->additional_values[1].magnitude;
                const TransitionDelays delays{
                    rise, fall, turnoff};
                if (dynamic_selection) {
                    process_.operations.emplace_back(
                        WriteInertialDynamicSlice{
                            signal->second,
                            *value,
                            *dynamic_selection,
                            delays});
                } else if (has_selected_offset) {
                    process_.operations.emplace_back(
                        WriteInertialSlice{
                            signal->second,
                            *value,
                            selected_offset,
                            delays});
                } else {
                    process_.operations.emplace_back(
                        WriteInertial{
                            signal->second, *value, delays});
                }
            } else if (dynamic_selection) {
                process_.operations.emplace_back(
                    WriteAfterDynamicSlice{
                        signal->second,
                        *value,
                        *dynamic_selection,
                        statement.delay->magnitude});
            } else if (has_selected_offset) {
                process_.operations.emplace_back(
                    WriteAfterSlice{
                        signal->second,
                        *value,
                        selected_offset,
                        statement.delay->magnitude});
            } else {
                process_.operations.emplace_back(WriteAfter{
                    signal->second,
                    *value,
                    statement.delay->magnitude});
            }
        } else if (
            statement.assignment_kind == AssignmentKind::Blocking) {
            if (dynamic_selection) {
                process_.operations.emplace_back(
                    WriteBlockingDynamicSlice{
                        signal->second,
                        *value,
                        *dynamic_selection});
            } else if (has_selected_offset) {
                process_.operations.emplace_back(WriteBlockingSlice{
                    signal->second, *value, selected_offset});
            } else {
                process_.operations.emplace_back(
                    WriteBlocking{signal->second, *value});
            }
        } else {
            if (dynamic_selection) {
                process_.operations.emplace_back(
                    WriteUpdateDynamicSlice{
                        signal->second,
                        *value,
                        *dynamic_selection});
            } else if (has_selected_offset) {
                process_.operations.emplace_back(WriteUpdateSlice{
                    signal->second, *value, selected_offset});
            } else {
                process_.operations.emplace_back(
                    WriteUpdate{signal->second, *value});
            }
        }
    }



    void Lowerer::lower_if(const Statement& statement) {
        const auto condition = lower_condition(
            statement.condition,
            statement.vhdl_conditional_assignment
                ? "FSIM-ELAB-092"
                : "FSIM-ELAB-048",
            statement.vhdl_conditional_assignment
                ? "conditional-assignment"
                : "if");
        if (!condition) {
            return;
        }
        const auto branch_index =
            static_cast<InstructionIndex>(process_.operations.size());
        const auto unknown_policy =
            language_ == frontend::Language::Vhdl2008
                ? UnknownBranchPolicy::error
                : UnknownBranchPolicy::when_false;
        process_.operations.emplace_back(
            Branch{*condition, 0, 0, unknown_policy});
        const auto true_start =
            static_cast<InstructionIndex>(process_.operations.size());
        lower_statements(statement.statements);
        const auto jump_index =
            static_cast<InstructionIndex>(process_.operations.size());
        process_.operations.emplace_back(Jump{0});
        const auto false_start =
            static_cast<InstructionIndex>(process_.operations.size());
        lower_statements(statement.else_statements);
        const auto end = static_cast<InstructionIndex>(process_.operations.size());
        process_.operations[branch_index] = Branch{
            *condition, true_start, false_start, unknown_policy};
        process_.operations[jump_index] = Jump{end};
    }



    void Lowerer::lower_case(const Statement& statement) {
        BinaryOperator match_operation = BinaryOperator::case_equal;
        switch (statement.case_match_kind) {
        case frontend::CaseMatchKind::Exact:
            break;
        case frontend::CaseMatchKind::WildcardZ:
            match_operation = BinaryOperator::casez_equal;
            break;
        case frontend::CaseMatchKind::WildcardXZ:
            match_operation = BinaryOperator::casex_equal;
            break;
        default:
            report(
                "FSIM-ELAB-081",
                "case statement has an invalid matching mode",
                statement.span);
            return;
        }
        const auto selector_width =
            infer_width(statement.condition).value_or(std::size_t{1});
        const auto* selector_type =
            statement.condition.kind
                    == ExpressionKind::Identifier
                ? object_type(statement.condition.text)
                : nullptr;
        const auto selector =
            lower_expression(
                statement.condition,
                selector_width,
                selector_type != nullptr
                        && !selector_type
                                ->enumeration_literals.empty()
                    ? selector_type
                    : nullptr);
        if (!selector) {
            return;
        }

        std::vector<InstructionIndex> exit_jumps;
        const frontend::CaseAlternative* default_alternative = nullptr;
        for (const auto& alternative : statement.case_alternatives) {
            if (alternative.is_default) {
                default_alternative = &alternative;
                continue;
            }

            std::vector<InstructionIndex> branches;
            for (const auto& choice : alternative.choices) {
                const auto choice_register =
                    lower_expression(
                        choice,
                        register_width(*selector),
                        selector_type != nullptr
                                && !selector_type
                                        ->enumeration_literals.empty()
                            ? selector_type
                            : nullptr);
                if (!choice_register) {
                    continue;
                }
                if (register_width(*choice_register)
                    != register_width(*selector)) {
                    report(
                        "FSIM-ELAB-063",
                        "case item width "
                            + std::to_string(
                                register_width(*choice_register))
                            + " does not match selector width "
                            + std::to_string(register_width(*selector)),
                        choice.span);
                    continue;
                }
                const auto condition =
                    allocate_register(1, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(Binary{
                    match_operation,
                    condition,
                    *selector,
                    *choice_register});
                branches.push_back(
                    static_cast<InstructionIndex>(
                        process_.operations.size()));
                process_.operations.emplace_back(Branch{
                    condition,
                    0,
                    0,
                    UnknownBranchPolicy::when_false});
            }

            const auto skip_body =
                static_cast<InstructionIndex>(process_.operations.size());
            process_.operations.emplace_back(Jump{0});
            const auto body_start =
                static_cast<InstructionIndex>(process_.operations.size());
            lower_statements(alternative.statements);
            exit_jumps.push_back(
                static_cast<InstructionIndex>(
                    process_.operations.size()));
            process_.operations.emplace_back(Jump{0});
            const auto next_alternative =
                static_cast<InstructionIndex>(process_.operations.size());

            for (std::size_t index = 0; index < branches.size(); ++index) {
                const auto false_target =
                    index + 1 < branches.size()
                        ? static_cast<InstructionIndex>(
                              branches[index] + 1)
                        : skip_body;
                const auto& operation =
                    std::get<Branch>(process_.operations[branches[index]]);
                process_.operations[branches[index]] = Branch{
                    operation.condition,
                    body_start,
                    false_target,
                    UnknownBranchPolicy::when_false};
            }
            process_.operations[skip_body] = Jump{next_alternative};
        }

        if (default_alternative != nullptr) {
            lower_statements(default_alternative->statements);
        }
        const auto end =
            static_cast<InstructionIndex>(process_.operations.size());
        for (const auto jump : exit_jumps) {
            process_.operations[jump] = Jump{end};
        }
    }

} // namespace fsim::elaboration
