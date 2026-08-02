// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

void Lowerer::validate_read_only_signal_writes(
    const frontend::SourceSpan& source) {
  std::set<SignalId> reported;
  const auto check = [&](const SignalId signal) {
    if (read_only_signals_.contains(signal)
        && reported.insert(signal).second) {
      report(
          "FSIM-ELAB-SVIFACE-006",
          "a modport input member is read-only within process '"
              + process_.name + "'",
          source);
    }
  };
  for (const auto& operation : process_.operations) {
    fsim::runtime::simir::visit_operation(
        [&](const auto& candidate) {
          using Operation = std::decay_t<decltype(candidate)>;
          if constexpr (
              std::is_same_v<Operation, WriteBlocking>
              || std::is_same_v<Operation, WriteUpdate>
              || std::is_same_v<Operation, WriteAfter>
              || std::is_same_v<Operation, WriteInertial>
              || std::is_same_v<Operation, WriteProjected>
              || std::is_same_v<Operation, WriteProjectedWaveform>
              || std::is_same_v<Operation, WriteBlockingSlice>
              || std::is_same_v<Operation, WriteUpdateSlice>
              || std::is_same_v<Operation, WriteAfterSlice>
              || std::is_same_v<Operation, WriteInertialSlice>
              || std::is_same_v<Operation, WriteProjectedSlice>
              || std::is_same_v<Operation, WriteProjectedWaveformSlice>) {
            check(candidate.signal);
          }
        },
        operation);
  }
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
                  const auto* type =
                      vhdl_array_attribute_prefix_type(candidate);
                  const auto dimension = candidate.operands.size() == 2
                      ? static_integer_value(candidate.operands[1]).value_or(1)
                      : std::int64_t{1};
                  const bool null_array = type != nullptr && type->vhdl_array
                      && dimension > 0
                      && static_cast<std::uint64_t>(dimension)
                          <= type->vhdl_array->dimensions.size()
                      && type->vhdl_array->dimensions[
                          static_cast<std::size_t>(dimension - 1)].null;
                  const auto width =
                      null_array ? std::uint64_t{0} : range->width();
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

    void Lowerer::lower_assignment(const Statement& statement) {
        if (lower_vhdl_access_assignment(statement)) {
            return;
        }
        const Expression* base = &statement.target;
        std::uint32_t selected_offset = 0;
        bool has_selected_offset = false;
        std::optional<std::size_t> selected_width;
        std::optional<frontend::ValueDomain> selected_domain;
        std::optional<DynamicIndex> dynamic_selection;
        std::optional<DynamicPartIndex> dynamic_part_selection;
        std::optional<frontend::Type> selected_type;
        std::vector<const Expression*> packed_selections;
        constexpr std::string_view vhdl_member_prefix{
            "@vhdl-member:"};
        while (base->kind == ExpressionKind::Index
               || base->kind == ExpressionKind::Slice
               || (base->kind == ExpressionKind::Call
                   && base->text.starts_with(vhdl_member_prefix))) {
            const auto expected_operands = base->kind == ExpressionKind::Index
                ? 2U : base->kind == ExpressionKind::Slice ? 3U : 1U;
            if (base->operands.size() != expected_operands) {
                break;
            }
            packed_selections.push_back(base);
            base = &base->operands.front();
        }
        std::ranges::reverse(packed_selections);
        if (base->kind != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-031",
                "an assignment target must be a packed object, bit-select, "
                "or constant part-select",
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
            if (read_only_container_objects_.contains(
                    target_name)) {
                report(
                    "FSIM-ELAB-SVPORT-009",
                    "an input container port is read-only within its "
                    "module",
                    statement.target.span);
                return;
            }
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
            const auto object =
                container_object != container_objects_.end()
                    ? std::optional<ContainerObjectId>{
                          container_object->second}
                    : std::nullopt;
            if (lower_multidimensional_container_assignment(
                    statement, *source_type, target, object)) {
                return;
            }
            if (packed_selections.size() > 1) {
                report(
                    "FSIM-ELAB-031",
                    "nested selected assignment targets require a complete "
                    "multidimensional static-array index",
                    statement.target.span);
                return;
            }
            if (statement.target.kind
                == ExpressionKind::Identifier) {
                const bool locator =
                    statement.value.kind == ExpressionKind::Call
                    && (statement.value.text == ".min"
                        || statement.value.text == ".max"
                        || statement.value.text == ".unique"
                        || statement.value.text == ".unique_index"
                        || statement.value.text == ".find"
                        || statement.value.text == ".find_index"
                        || statement.value.text == ".find_first"
                        || statement.value.text
                            == ".find_first_index"
                        || statement.value.text == ".find_last"
                        || statement.value.text
                            == ".find_last_index");
                if (locator) {
                    if (!lower_container_locator(
                            statement.value, target, *runtime_type)) {
                        return;
                    }
                } else if (statement.value.kind
                        == ExpressionKind::Aggregate
                    && statement.value.text == "sv-pattern") {
                    const auto value = lower_container_pattern(
                        statement.value,
                        *source_type,
                        *runtime_type);
                    if (!value) {
                        return;
                    }
                    process_.operations.emplace_back(
                        CopyContainerRegister{target, *value});
                } else if (
                    runtime_type->fixed
                    && (statement.value.kind
                            == ExpressionKind::Slice
                        || (statement.value.kind
                                == ExpressionKind::Call
                            && statement.value.text != "?:"))) {
                    const auto value =
                        lower_static_container_assignment_value(
                            statement.value, *runtime_type);
                    if (!value) {
                        return;
                    }
                    process_.operations.emplace_back(
                        CopyContainerRegister{target, *value});
                } else {
                    lower_nonstatic_container_assignment(
                        target, statement.value, *runtime_type);
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
                auto index = lower_expression(
                    statement.target.operands[1],
                    index_width,
                    index_type);
                auto element_type = *source_type;
                element_type.systemverilog_container.reset();
                auto value = lower_expression(
                    statement.value, *element_width, &element_type);
                if (!index || !value) {
                    return;
                }
                if (register_width(*value) != *element_width) {
                    *value = resize_register(
                        *value,
                        *element_width,
                        is_signed_expression(statement.value));
                }
                if (runtime_type->associative
                    && register_width(*index)
                        != runtime_type->index_width) {
                    *index = resize_register(
                        *index,
                        runtime_type->index_width,
                        runtime_type->signed_indices);
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
            } else if (
                statement.target.kind == ExpressionKind::Slice
                && statement.target.operands.size() == 3) {
                if (!lower_static_container_slice_assignment(
                        statement.target,
                        statement.value,
                        target,
                        *runtime_type)) {
                    return;
                }
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
            if (string_object != string_objects_.end()
                && read_only_string_objects_.contains(
                    string_object->second)) {
                report(
                    "FSIM-ELAB-SVPORT-011",
                    "an input mutable string port is read-only",
                    statement.target.span);
                return;
            }
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
                    || selected->lsb_offset
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
                    selected->lsb_offset);
                has_selected_offset = true;
                selected_width = static_cast<std::size_t>(*width);
                selected_domain = selected->member->domain;
                if (language_ == frontend::Language::Vhdl2008
                    && !selected->member->nested_types.empty()) {
                    selected_type =
                        selected->member->nested_types.front();
                }
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
        if (!lower_assignment_selections(
                statement, base->text, whole_width,
                packed_selections, selected_offset,
                has_selected_offset, selected_width,
                selected_domain, dynamic_selection,
                dynamic_part_selection, selected_type)) {
            return;
        }

        const auto target_width =
            selected_width.value_or(whole_width);
        const bool has_disconnect = std::ranges::any_of(
            statement.vhdl_waveform,
            &frontend::VhdlWaveformElement::disconnect);
        const auto disconnect_domain = selected_domain.value_or(
            local != locals_.end()
                ? register_domain(local->second)
                : design_.signal_info_[signal->second].source_domain);
        if (has_disconnect
            && (local != locals_.end()
                || disconnect_domain
                    != frontend::ValueDomain::Logic9)) {
            report(
                "FSIM-ELAB-VHDLGUARD-002",
                "guarded driver disconnection requires a nine-state "
                "resolved signal target",
                statement.span);
            return;
        }
        const auto load_disconnect = [&]() {
            const auto result = allocate_register(
                target_width, frontend::ValueDomain::Logic9);
            auto value = PackedLogic4{
                target_width, Logic4::z};
            value.fill(runtime::Logic9::z);
            process_.operations.emplace_back(
                LoadConstant{result, std::move(value)});
            return result;
        };
        if (dynamic_part_selection
            && language_ != frontend::Language::Vhdl2008
            && statement.assignment_kind != AssignmentKind::Blocking
            && statement.assignment_kind != AssignmentKind::NonBlocking) {
            report(
                "FSIM-ELAB-SVEXPR-006",
                "runtime-base packed part-select targets require a "
                "procedural blocking or nonblocking assignment",
                statement.target.span);
            return;
        }
        const auto* contextual_target_type = selected_type
            ? &*selected_type
            : has_selected_offset || dynamic_selection
                    || dynamic_part_selection
                ? nullptr
                : object_type(target_name);
        if (language_ == frontend::Language::Vhdl2008
            && contextual_target_type != nullptr
            && contextual_target_type->vhdl_access
            && signal != signals_.end()
            && !(statement.value.kind == ExpressionKind::Call
                 && statement.value.text == "@vhdl-null")) {
            report(
                "FSIM-ELAB-VHACCESS-021",
                "a nonnull process-local VHDL access handle cannot escape "
                "through a signal",
                statement.value.span);
            return;
        }
        if (!has_disconnect
            && !validate_sv_nominal_assignment(
                contextual_target_type, statement.value)) {
            return;
        }
        if (!has_disconnect
            && !validate_vhdl_composite_assignment(
                contextual_target_type, statement.value)) {
            return;
        }
        const bool null_vhdl_target =
            language_ == frontend::Language::Vhdl2008
            && target_width == 0
            && contextual_target_type != nullptr
            && contextual_target_type->vhdl_array
            && contextual_target_type->vhdl_array->flat_width
            && *contextual_target_type->vhdl_array->flat_width == 0;
        if (null_vhdl_target) {
            const auto value = lower_expression(
                statement.value, 0, contextual_target_type);
            if (!value) {
                return;
            }
            if (register_width(*value) != 0) {
                report(
                    "FSIM-ELAB-047",
                    "assignment to null VHDL array '" + target_name
                        + "' requires a null array value",
                    statement.span);
            }
            return;
        }
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
        if (statement.procedural_assignment_repeat
            && (!procedural_event
                || !statement.loop_limit.valid())) {
            report(
                "FSIM-ELAB-105",
                "repeated procedural assignment event control has "
                "invalid control or count metadata",
                statement.span);
            return;
        }
        if (procedural_event) {
            emit_debug_point(
                DebugPointKind::wait, statement.span);
            if (!emit_event_control_wait(statement)) {
                return;
            }
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
            std::optional<RegisterId> captured;
            if (statement.procedural_update_kind
                != frontend::ProceduralUpdateKind::None) {
                if (dynamic_part_selection) {
                    captured = allocate_register(
                        target_width,
                        selected_domain.value_or(
                            register_domain(local->second)));
                    process_.operations.emplace_back(
                        DynamicPartSelect{
                            *captured,
                            local->second,
                            dynamic_part_selection->base,
                            dynamic_part_selection->left,
                            dynamic_part_selection->right,
                            dynamic_part_selection->width,
                            dynamic_part_selection->increasing,
                            dynamic_part_selection->source_descending,
                            is_two_state_domain(
                                register_domain(local->second)),
                            dynamic_part_selection->base_offset});
                } else if (dynamic_selection) {
                    captured = allocate_register(
                        target_width,
                        selected_domain.value_or(
                            register_domain(local->second)));
                    process_.operations.emplace_back(
                        DynamicExtract{
                            *captured,
                            local->second,
                            *dynamic_selection});
                } else if (has_selected_offset) {
                    captured = allocate_register(
                        target_width,
                        selected_domain.value_or(
                            register_domain(local->second)));
                    process_.operations.emplace_back(Extract{
                        *captured,
                        local->second,
                        selected_offset,
                        static_cast<std::uint32_t>(target_width)});
                } else {
                    captured = allocate_register(
                        target_width,
                        register_domain(local->second));
                    process_.operations.emplace_back(
                        CopyRegister{*captured, local->second});
                }
            }
            auto value = captured
                ? lower_procedural_update_value(
                      statement,
                      *captured,
                      target_width,
                      contextual_target_type)
                : lower_expression(
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
                && !dynamic_selection
                && !dynamic_part_selection) {
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
                && !dynamic_part_selection
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
            if (dynamic_part_selection) {
                process_.operations.emplace_back(DynamicPartInsert{
                    local->second,
                    local->second,
                    *value,
                    *dynamic_part_selection});
            } else if (dynamic_selection) {
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
                if (!element.disconnect
                    && !validate_vhdl_composite_assignment(
                        contextual_target_type, element.value)) {
                    return;
                }
                const auto value = element.disconnect
                    ? std::optional<RegisterId>{load_disconnect()}
                    : lower_expression(
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
                if (!element.disconnect
                    && language_ == frontend::Language::Vhdl2008
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
                if (!element.disconnect
                    && language_
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
            if (dynamic_part_selection) {
                process_.operations.emplace_back(
                    WriteProjectedWaveformDynamicSlice{
                        signal->second,
                        std::move(waveform),
                        DynamicIndex{
                            dynamic_part_selection->base,
                            dynamic_part_selection->left,
                            dynamic_part_selection->right,
                            dynamic_part_selection->base_offset},
                        rejection,
                        mode});
            } else if (dynamic_selection) {
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
        std::optional<RegisterId> captured;
        if (statement.procedural_update_kind
            != frontend::ProceduralUpdateKind::None) {
            const auto* target_type = visible_type(target_name);
            const auto target_domain = selected_domain.value_or(
                target_type != nullptr
                    ? target_type->domain
                    : design_.signal_info_[signal->second]
                          .source_domain);
            const auto whole = allocate_register(
                whole_width,
                target_type != nullptr
                    ? target_type->domain
                    : design_.signal_info_[signal->second]
                          .source_domain);
            process_.operations.emplace_back(
                ReadSignal{whole, signal->second});
            if (dynamic_part_selection) {
                captured = allocate_register(
                    target_width, target_domain);
                process_.operations.emplace_back(DynamicPartSelect{
                    *captured,
                    whole,
                    dynamic_part_selection->base,
                    dynamic_part_selection->left,
                    dynamic_part_selection->right,
                    dynamic_part_selection->width,
                    dynamic_part_selection->increasing,
                    dynamic_part_selection->source_descending,
                    is_two_state_domain(target_domain),
                    dynamic_part_selection->base_offset});
            } else if (dynamic_selection) {
                captured = allocate_register(
                    target_width, target_domain);
                process_.operations.emplace_back(DynamicExtract{
                    *captured,
                    whole,
                    *dynamic_selection});
            } else if (has_selected_offset) {
                captured = allocate_register(
                    target_width, target_domain);
                process_.operations.emplace_back(Extract{
                    *captured,
                    whole,
                    selected_offset,
                    static_cast<std::uint32_t>(target_width)});
            } else {
                captured = whole;
            }
        }
        const bool disconnect = statement.vhdl_waveform.size() == 1
            && statement.vhdl_waveform.front().disconnect;
        auto value = disconnect
            ? std::optional<RegisterId>{load_disconnect()}
            : captured
                  ? lower_procedural_update_value(
                        statement,
                        *captured,
                        target_width,
                        contextual_target_type)
                  : lower_expression(
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
        if (language_ == frontend::Language::Vhdl2008
            && target_type != nullptr
            && target_type->vhdl_physical) {
            const auto source_type =
                vhdl_expression_type(statement.value);
            const bool physical_literal =
                statement.value.kind == ExpressionKind::Call
                && statement.value.text.starts_with(
                    "@vhdl-physical:");
            const bool physical_operation =
                statement.value.kind == ExpressionKind::Binary;
            if (source_type && source_type->vhdl_physical
                && source_type->nominal_type
                    != target_type->nominal_type) {
                report(
                    "FSIM-ELAB-VHPHYSICAL-009",
                    "assignment between distinct nominal physical types "
                    "is not legal",
                    statement.span);
                return;
            }
            if ((!source_type || !source_type->vhdl_physical)
                && !physical_literal && !physical_operation) {
                report(
                    "FSIM-ELAB-VHPHYSICAL-010",
                    "a physical target requires a same-type value, "
                    "physical literal, physical operation, or explicit "
                    "type conversion",
                    statement.span);
                return;
            }
        }
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
        if (!disconnect
            && language_ == frontend::Language::Vhdl2008
            && target_domain == frontend::ValueDomain::Integer
            && !has_selected_offset
            && !dynamic_selection
            && !dynamic_part_selection) {
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
        if (!disconnect
            && language_ == frontend::Language::Vhdl2008
            && !has_selected_offset
            && !dynamic_selection
            && !dynamic_part_selection
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
            if (dynamic_part_selection) {
                process_.operations.emplace_back(
                    WriteBlockingDynamicPartSlice{
                        signal->second,
                        *value,
                        *dynamic_part_selection});
            } else if (dynamic_selection) {
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
            if (dynamic_part_selection) {
                process_.operations.emplace_back(
                    WriteProjectedDynamicSlice{
                        signal->second,
                        *value,
                        DynamicIndex{
                            dynamic_part_selection->base,
                            dynamic_part_selection->left,
                            dynamic_part_selection->right,
                            dynamic_part_selection->base_offset},
                        delay,
                        rejection,
                        mode});
            } else if (dynamic_selection) {
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
            } else if (dynamic_part_selection) {
                process_.operations.emplace_back(
                    WriteAfterDynamicPartSlice{
                        signal->second,
                        *value,
                        *dynamic_part_selection,
                        statement.delay->magnitude});
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
            if (dynamic_part_selection) {
                process_.operations.emplace_back(
                    WriteBlockingDynamicPartSlice{
                        signal->second,
                        *value,
                        *dynamic_part_selection});
            } else if (dynamic_selection) {
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
            if (dynamic_part_selection) {
                process_.operations.emplace_back(
                    WriteUpdateDynamicPartSlice{
                        signal->second,
                        *value,
                        *dynamic_part_selection});
            } else if (dynamic_selection) {
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



    bool Lowerer::is_bounded_case_pattern_constant(
        const Expression& expression) const {
        switch (expression.kind) {
        case ExpressionKind::IntegerLiteral:
        case ExpressionKind::BooleanLiteral:
        case ExpressionKind::LogicLiteral:
            return true;
        case ExpressionKind::Unary:
        case ExpressionKind::Update:
        case ExpressionKind::Binary:
        case ExpressionKind::Concatenation:
        case ExpressionKind::Replication:
            return std::ranges::all_of(
                expression.operands,
                [this](const Expression& operand) {
                    return is_bounded_case_pattern_constant(operand);
                });
        case ExpressionKind::Call:
            return (expression.text == "?:"
                    || expression.text == "$signed"
                    || expression.text == "$unsigned"
                    || expression.text == "$clog2")
                && std::ranges::all_of(
                    expression.operands,
                    [this](const Expression& operand) {
                        return is_bounded_case_pattern_constant(operand);
                    });
        default:
            return false;
        }
    }



    void Lowerer::lower_case(const Statement& statement) {
        if (statement.case_qualifier
            != frontend::CaseQualifier::None) {
            lower_qualified_case(statement);
            return;
        }
        BinaryOperator match_operation = BinaryOperator::case_equal;
        bool inside_matching = false;
        bool pattern_matching = false;
        bool vhdl_matching = false;
        switch (statement.case_match_kind) {
        case frontend::CaseMatchKind::Exact:
            break;
        case frontend::CaseMatchKind::WildcardZ:
            match_operation = BinaryOperator::casez_equal;
            break;
        case frontend::CaseMatchKind::WildcardXZ:
            match_operation = BinaryOperator::casex_equal;
            break;
        case frontend::CaseMatchKind::Inside:
            inside_matching = true;
            match_operation = BinaryOperator::wildcard_equal;
            break;
        case frontend::CaseMatchKind::Matches:
            pattern_matching = true;
            break;
        case frontend::CaseMatchKind::VhdlMatching:
            vhdl_matching = true;
            match_operation = BinaryOperator::vhdl_match_equal;
            break;
        default:
            report(
                "FSIM-ELAB-081",
                "case statement has an invalid matching mode",
                statement.span);
            return;
        }
        if (inside_matching
            && language_ != frontend::Language::SystemVerilog2017) {
            report(
                "FSIM-ELAB-SVCASEINSIDE-001",
                "case inside matching requires SystemVerilog",
                statement.span);
            return;
        }
        if (pattern_matching
            && language_ != frontend::Language::SystemVerilog2017) {
            report(
                "FSIM-ELAB-SVMATCH-001",
                "case matches pattern matching requires SystemVerilog",
                statement.span);
            return;
        }
        if ((inside_matching || pattern_matching)
            && (is_container_expression(statement.condition)
                || is_string_expression(statement.condition)
                || statement.condition.kind == ExpressionKind::Aggregate
                || statement.condition.kind
                    == ExpressionKind::Concatenation)) {
            report(
                pattern_matching
                    ? "FSIM-ELAB-SVMATCH-002"
                    : "FSIM-ELAB-SVCASEINSIDE-002",
                pattern_matching
                    ? "bounded case matches requires a scalar integral "
                      "selector"
                    : "bounded case inside requires a scalar integral "
                      "selector",
                statement.condition.span);
            return;
        }
        const auto inferred_selector_width =
            infer_width(statement.condition);
        if ((inside_matching || pattern_matching)
            && (!inferred_selector_width
                || *inferred_selector_width == 0)) {
            report(
                pattern_matching
                    ? "FSIM-ELAB-SVMATCH-002"
                    : "FSIM-ELAB-SVCASEINSIDE-002",
                pattern_matching
                    ? "the case matches selector width is not statically "
                      "inferable"
                    : "the case inside selector width is not statically "
                      "inferable",
                statement.condition.span);
            return;
        }
        const auto selector_width = inferred_selector_width.value_or(
            std::size_t{1});
        const bool selector_signed =
            is_signed_expression(statement.condition);
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
        if (vhdl_matching
            && !validate_vhdl_matching_case(
                statement,
                selector_type,
                selector_width,
                register_domain(*selector))) {
            return;
        }
        if (!vhdl_matching
            && language_ == frontend::Language::Vhdl2008
            && !validate_vhdl_case_choices(
                statement,
                selector_type,
                selector_width,
                register_domain(*selector))) {
            return;
        }

        const auto lower_inside_operand =
            [&](const Expression& operand)
                -> std::optional<RegisterId> {
              if (is_container_expression(operand)
                  || is_string_expression(operand)
                  || operand.kind == ExpressionKind::Aggregate
                  || operand.kind == ExpressionKind::Concatenation
                  || (operand.kind == ExpressionKind::Call
                      && (operand.text == "inside"
                          || operand.text == "@inside-range"))) {
                report(
                    "FSIM-ELAB-SVCASEINSIDE-003",
                    "case inside choices must be nonnested scalar integral "
                    "values",
                    operand.span);
                return std::nullopt;
              }
              const auto width = infer_width(operand);
              if (!width || *width != selector_width
                  || is_signed_expression(operand) != selector_signed) {
                report(
                    "FSIM-ELAB-SVCASEINSIDE-004",
                    "case inside choices and range bounds must exactly match "
                    "the selector width and signedness",
                    operand.span);
                return std::nullopt;
              }
              return lower_expression(operand, selector_width);
            };

        std::vector<InstructionIndex> exit_jumps;
        const frontend::CaseAlternative* default_alternative = nullptr;
        for (const auto& alternative : statement.case_alternatives) {
            if (alternative.is_default) {
                default_alternative = &alternative;
                continue;
            }

            if (inside_matching && alternative.choices.empty()) {
                report(
                    "FSIM-ELAB-SVCASEINSIDE-005",
                    "a case inside alternative requires at least one choice",
                    alternative.span);
                continue;
            }
            if (pattern_matching && alternative.choices.size() != 1) {
                report(
                    "FSIM-ELAB-SVMATCH-005",
                    "a case matches item requires exactly one pattern",
                    alternative.span);
                continue;
            }

            std::vector<InstructionIndex> branches;
            for (const auto& choice : alternative.choices) {
                std::optional<RegisterId> condition;
                if (language_ == frontend::Language::Vhdl2008
                    && choice.kind == ExpressionKind::Call
                    && (choice.text == "@vhdl-case-range-to"
                        || choice.text
                            == "@vhdl-case-range-downto")) {
                    condition = lower_vhdl_case_range_condition(
                        choice,
                        *selector,
                        selector_type,
                        selector_signed);
                } else if (pattern_matching
                    && choice.kind == ExpressionKind::Call
                    && choice.text == "@match-wildcard") {
                    condition = allocate_register(
                        1, frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(LoadConstant{
                        *condition, PackedLogic4(1, Logic4::one)});
                } else if (pattern_matching) {
                    if (!is_bounded_case_pattern_constant(choice)) {
                        report(
                            "FSIM-ELAB-SVMATCH-003",
                            "bounded case matches patterns must be scalar "
                            "integral constants or '.*'",
                            choice.span);
                        continue;
                    }
                    const auto width = infer_width(choice);
                    if (!width || *width != selector_width
                        || is_signed_expression(choice)
                            != selector_signed) {
                        report(
                            "FSIM-ELAB-SVMATCH-004",
                            "case matches constants must exactly match the "
                            "selector width and signedness",
                            choice.span);
                        continue;
                    }
                    const auto pattern = lower_expression(
                        choice, selector_width);
                    if (!pattern) {
                        continue;
                    }
                    condition = allocate_register(
                        1, frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(Binary{
                        BinaryOperator::case_equal,
                        *condition,
                        *selector,
                        *pattern});
                } else if (inside_matching
                    && choice.kind == ExpressionKind::Call
                    && choice.text == "@inside-range") {
                    if (choice.operands.size() != 2) {
                        report(
                            "FSIM-ELAB-SVCASEINSIDE-006",
                            "a case inside range requires exactly one low "
                            "and high bound",
                            choice.span);
                        continue;
                    }
                    const auto low =
                        lower_inside_operand(choice.operands[0]);
                    const auto high =
                        lower_inside_operand(choice.operands[1]);
                    if (!low || !high) {
                        continue;
                    }
                    const auto domain = frontend::ValueDomain::Logic4;
                    const auto valid = allocate_register(1, domain);
                    const auto above_low = allocate_register(1, domain);
                    const auto below_high = allocate_register(1, domain);
                    const auto within_lower = allocate_register(1, domain);
                    condition = allocate_register(1, domain);
                    process_.operations.emplace_back(Binary{
                        selector_signed
                            ? BinaryOperator::less_equal_signed
                            : BinaryOperator::less_equal_unsigned,
                        valid, *low, *high});
                    process_.operations.emplace_back(Binary{
                        selector_signed
                            ? BinaryOperator::greater_equal_signed
                            : BinaryOperator::greater_equal_unsigned,
                        above_low, *selector, *low});
                    process_.operations.emplace_back(Binary{
                        selector_signed
                            ? BinaryOperator::less_equal_signed
                            : BinaryOperator::less_equal_unsigned,
                        below_high, *selector, *high});
                    process_.operations.emplace_back(LogicalBinary{
                        LogicalBinaryOperator::logical_and,
                        within_lower, valid, above_low});
                    process_.operations.emplace_back(LogicalBinary{
                        LogicalBinaryOperator::logical_and,
                        *condition, within_lower, below_high});
                } else {
                    const auto choice_register = inside_matching
                        ? lower_inside_operand(choice)
                        : lower_expression(
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
                    if (!inside_matching
                        && register_width(*choice_register)
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
                    condition = allocate_register(
                        1,
                        inside_matching
                            ? frontend::ValueDomain::Logic4
                            : frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(Binary{
                        match_operation,
                        *condition,
                        *selector,
                        *choice_register});
                }
                if (!condition) {
                    continue;
                }
                branches.push_back(
                    static_cast<InstructionIndex>(
                        process_.operations.size()));
                process_.operations.emplace_back(Branch{
                    *condition,
                    0,
                    0,
                    UnknownBranchPolicy::when_false});
            }

            const auto skip_body =
                static_cast<InstructionIndex>(process_.operations.size());
            process_.operations.emplace_back(Jump{0});
            const auto body_start =
                static_cast<InstructionIndex>(process_.operations.size());
            lower_case_alternative(alternative);
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
                    fsim::runtime::simir::operation_get<Branch>(process_.operations[branches[index]]);
                process_.operations[branches[index]] = Branch{
                    operation.condition,
                    body_start,
                    false_target,
                    UnknownBranchPolicy::when_false};
            }
            process_.operations[skip_body] = Jump{next_alternative};
        }

        if (default_alternative != nullptr) {
            lower_case_alternative(*default_alternative);
        }
        const auto end =
            static_cast<InstructionIndex>(process_.operations.size());
        for (const auto jump : exit_jumps) {
            process_.operations[jump] = Jump{end};
        }
    }

} // namespace fsim::elaboration
