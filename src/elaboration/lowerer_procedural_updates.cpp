// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {

using namespace runtime::simir;

std::optional<DynamicIndex> Lowerer::lower_dynamic_index(
    const Expression& source,
    const Expression& index,
    const std::size_t source_width,
    const std::uint32_t base_offset,
    const frontend::SourceSpan& span)
{
    const auto range = expression_range(source, source_width);
    if (!range
        || range->left < std::numeric_limits<std::int32_t>::min()
        || range->left > std::numeric_limits<std::int32_t>::max()
        || range->right < std::numeric_limits<std::int32_t>::min()
        || range->right > std::numeric_limits<std::int32_t>::max()
        || range->width()
            > static_cast<std::uint64_t>(
                  std::numeric_limits<std::uint32_t>::max() - base_offset)
                + 1U) {
        report(
            "FSIM-ELAB-DYNINDEX-001",
            "dynamic packed selection requires a concrete one-dimensional range "
            "representable by signed 32-bit indices and normalized offsets",
            span);
        return std::nullopt;
    }
    if (language_ == frontend::Language::Vhdl2008
        && !is_integer_expression(index)) {
        report(
            "FSIM-ELAB-DYNINDEX-002",
            "a dynamic VHDL array index requires an integer-family expression",
            index.span);
        return std::nullopt;
    }
    // A packed-select index is a self-determined expression.  Lower it at
    // its own SystemVerilog width before normalizing the completed value to
    // the runtime's signed 32-bit index representation.  Passing the runtime
    // width down as expression context widens operations such as a 2-bit
    // `slot + 1'b1`, preventing the required modulo-4 wrap at the select.
    const auto index_width = language_ == frontend::Language::SystemVerilog2017
        ? infer_width(index).value_or(std::size_t { 32 })
        : std::size_t { 32 };
    const auto lowered = lower_expression(index, index_width);
    if (!lowered) {
        report(
            "FSIM-ELAB-DYNINDEX-002",
            "a dynamic packed index must lower to the signed 32-bit runtime "
            "representation",
            index.span);
        return std::nullopt;
    }
    const auto normalized = resize_register(
        *lowered, 32, is_signed_expression(index));
    return DynamicIndex {
        normalized, range->left, range->right, base_offset
    };
}

std::optional<DynamicPartIndex> Lowerer::lower_vhdl_dynamic_slice(
    const Expression& expression,
    const std::size_t source_width,
    const std::size_t selected_width,
    const std::uint32_t base_offset)
{
    if (language_ != frontend::Language::Vhdl2008
        || expression.kind != ExpressionKind::Slice
        || expression.operands.size() != 3
        || (expression.text != "to" && expression.text != "downto")) {
        return std::nullopt;
    }
    const auto range = expression_range(
        expression.operands.front(), source_width);
    const bool descending = expression.text == "downto";
    if (!range
        || range->left < std::numeric_limits<std::int32_t>::min()
        || range->left > std::numeric_limits<std::int32_t>::max()
        || range->right < std::numeric_limits<std::int32_t>::min()
        || range->right > std::numeric_limits<std::int32_t>::max()
        || range->descending != descending
        || selected_width == 0
        || selected_width - 1U
            > static_cast<std::size_t>(
                std::numeric_limits<std::int32_t>::max())) {
        report(
            "FSIM-ELAB-VHSLICE-001",
            "a dynamic VHDL slice requires a nonempty width representable by "
            "its signed 32-bit distance, a concrete signed 32-bit source "
            "range, and matching direction",
            expression.span);
        return std::nullopt;
    }
    const auto& left_expression = expression.operands[1];
    const auto& right_expression = expression.operands[2];
    if (!is_integer_expression(left_expression)
        || !is_integer_expression(right_expression)) {
        report(
            "FSIM-ELAB-VHSLICE-002",
            "dynamic VHDL slice bounds require integer-family expressions",
            expression.span);
        return std::nullopt;
    }
    const auto left = lower_expression(left_expression, 32);
    const auto right = lower_expression(right_expression, 32);
    if (!left || !right || register_width(*left) != 32
        || register_width(*right) != 32) {
        report(
            "FSIM-ELAB-VHSLICE-002",
            "dynamic VHDL slice bounds must lower to signed 32-bit values",
            expression.span);
        return std::nullopt;
    }
    const auto lower = static_cast<std::int32_t>(
        std::min(range->left, range->right));
    const auto upper = static_cast<std::int32_t>(
        std::max(range->left, range->right));
    process_.operations.emplace_back(IntegerCheck { *left, lower, upper });
    process_.operations.emplace_back(IntegerCheck { *right, lower, upper });
    const auto distance = allocate_register(
        32, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(IntegerBinary {
        IntegerBinaryOperator::subtract,
        distance,
        descending ? *left : *right,
        descending ? *right : *left });
    const auto required_distance = static_cast<std::int32_t>(
        selected_width - 1U);
    process_.operations.emplace_back(IntegerCheck {
        distance, required_distance, required_distance });
    return DynamicPartIndex {
        *right,
        range->left,
        range->right,
        base_offset,
        static_cast<std::uint32_t>(selected_width),
        descending,
        range->descending
    };
}

std::optional<RegisterId>
Lowerer::lower_vhdl_dynamic_slice_expression(
    const Expression& expression,
    const std::size_t source_width,
    const std::size_t selected_width)
{
    const auto selection = lower_vhdl_dynamic_slice(
        expression, source_width, selected_width, 0);
    const auto source = selection
        ? lower_expression(expression.operands.front(), source_width)
        : std::nullopt;
    if (!selection || !source) {
        return std::nullopt;
    }
    const auto destination = allocate_register(
        selected_width, register_domain(*source));
    process_.operations.emplace_back(DynamicPartSelect {
        destination,
        *source,
        selection->base,
        selection->left,
        selection->right,
        selection->width,
        selection->increasing,
        selection->source_descending,
        is_two_state_domain(register_domain(*source)),
        selection->base_offset });
    return destination;
}

std::optional<Lowerer::ConstantSliceSelection>
Lowerer::constant_slice_selection(
    const Expression& expression,
    const std::size_t source_width)
{
    if (expression.kind != ExpressionKind::Slice
        || expression.operands.size() != 3) {
        return std::nullopt;
    }
    const auto& source = expression.operands[0];
    const auto range = expression_range(source, source_width);
    if (!range) {
        return std::nullopt;
    }

    std::int64_t left = 0;
    std::int64_t right = 0;
    std::uint64_t width = 0;
    if (expression.text == "+:" || expression.text == "-:") {
        const auto base = static_integer_value(expression.operands[1]);
        const auto selected_width = static_integer_value(expression.operands[2]);
        if (!base || !selected_width || *selected_width <= 0) {
            return std::nullopt;
        }
        const auto distance = *selected_width - 1;
        std::int64_t lower = 0;
        std::int64_t upper = 0;
        if (expression.text == "+:") {
            if (*base
                > std::numeric_limits<std::int64_t>::max() - distance) {
                return std::nullopt;
            }
            lower = *base;
            upper = *base + distance;
        } else {
            if (*base
                < std::numeric_limits<std::int64_t>::min() + distance) {
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
        const auto parsed_left = static_integer_value(expression.operands[1]);
        const auto parsed_right = static_integer_value(expression.operands[2]);
        if (!parsed_left || !parsed_right) {
            return std::nullopt;
        }
        left = *parsed_left;
        right = *parsed_right;
        const bool selected_descending = left >= right;
        if (left != right
            && selected_descending != (range->left >= range->right)) {
            return std::nullopt;
        }
        width = index_distance(left, right) + 1;
    }
    const auto offset = select_offset(source, right, source_width);
    const auto left_offset = select_offset(source, left, source_width);
    if (!offset || !left_offset || width == 0
        || width > std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }
    return ConstantSliceSelection {
        *offset, static_cast<std::size_t>(width)
    };
}

std::optional<RegisterId> Lowerer::lower_procedural_update_value(
    const Statement& statement,
    const RegisterId captured,
    const std::size_t target_width,
    const frontend::Type* contextual_target_type)
{
    if (statement.procedural_update_kind
            == frontend::ProceduralUpdateKind::None
        || statement.procedural_update_operator.empty()
        || statement.value.kind != ExpressionKind::Binary
        || statement.value.operands.size() != 2
        || statement.value.text
            != statement.procedural_update_operator) {
        report(
            "FSIM-ELAB-106",
            "procedural update metadata does not match its normalized binary "
            "expression",
            statement.span);
        return std::nullopt;
    }
    const auto temporary_name = "@procedural-lvalue-" + std::to_string(captured) + "-"
        + std::to_string(process_.operations.size());
    const auto [local, inserted] = locals_.emplace(temporary_name, captured);
    if (!inserted) {
        report(
            "FSIM-ELAB-106",
            "procedural update could not allocate its captured lvalue",
            statement.span);
        return std::nullopt;
    }
    local_signed_.emplace(
        temporary_name, is_signed_expression(statement.target));
    if (target_width > 0
        && target_width - 1
            <= static_cast<std::size_t>(
                std::numeric_limits<std::int64_t>::max())) {
        local_ranges_.emplace(
            temporary_name,
            frontend::PackedRange {
                static_cast<std::int64_t>(target_width - 1), 0, true });
    }
    auto normalized = statement.value;
    normalized.operands[0] = Expression {
        ExpressionKind::Identifier,
        temporary_name,
        { },
        statement.target.span
    };
    const auto value = lower_expression(
        normalized, target_width, contextual_target_type);
    local_ranges_.erase(temporary_name);
    local_signed_.erase(temporary_name);
    locals_.erase(local);
    if (value) {
        procedural_update_result_ = statement.procedural_update_kind
                == frontend::ProceduralUpdateKind::Postfix
            ? captured
            : *value;
    }
    return value;
}

std::optional<RegisterId>
Lowerer::lower_procedural_update_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type)
{
    if (language_ != frontend::Language::SystemVerilog2017
        || expression.operands.size() != 1
        || (expression.text != "pre++"
            && expression.text != "pre--"
            && expression.text != "post++"
            && expression.text != "post--")) {
        report(
            "FSIM-ELAB-SVEXPR-007",
            "an update expression requires a SystemVerilog prefix or postfix "
            "increment/decrement with one writable operand",
            expression.span);
        return std::nullopt;
    }
    const bool increment = expression.text.ends_with("++");
    const bool prefix = expression.text.starts_with("pre");
    const auto one = Expression {
        ExpressionKind::IntegerLiteral, "1", { }, expression.span
    };
    Statement update;
    update.kind = StatementKind::Assignment;
    update.assignment_kind = AssignmentKind::Blocking;
    update.target = expression.operands.front();
    update.value = Expression {
        ExpressionKind::Binary,
        increment ? "+" : "-",
        { update.target, one },
        expression.span
    };
    update.procedural_update_kind = prefix ? frontend::ProceduralUpdateKind::Prefix
                                           : frontend::ProceduralUpdateKind::Postfix;
    update.procedural_update_operator = increment ? "+" : "-";
    update.span = expression.span;

    procedural_update_result_.reset();
    lower_assignment(update);
    if (!procedural_update_result_) {
        return std::nullopt;
    }
    auto result = *procedural_update_result_;
    procedural_update_result_.reset();
    if (expected_width != 0
        && register_width(result) != expected_width) {
        result = resize_register(
            result,
            expected_width,
            is_signed_expression(expression));
    }
    (void)expected_type;
    return result;
}

void Lowerer::lower_force_release(const Statement& statement)
{
    if (statement.target.kind == ExpressionKind::Concatenation) {
        lower_concatenated_force_release(statement);
        return;
    }
    const bool force = statement.kind == StatementKind::Force;
    const bool vhdl = language_ == frontend::Language::Vhdl2008;
    const auto target_code = vhdl ? "FSIM-ELAB-VHFORCE-001" : "FSIM-ELAB-SVFORCE-001";
    const auto visibility_code = vhdl ? "FSIM-ELAB-VHFORCE-002" : "FSIM-ELAB-SVFORCE-002";
    const auto domain_code = vhdl ? "FSIM-ELAB-VHFORCE-003" : "FSIM-ELAB-SVFORCE-003";
    const auto language_name = vhdl ? std::string_view { "VHDL" }
                                    : std::string_view { "procedural" };
    const Expression* base = &statement.target;
    if ((statement.target.kind == ExpressionKind::Index
            && statement.target.operands.size() == 2)
        || (statement.target.kind == ExpressionKind::Slice
            && statement.target.operands.size() == 3)) {
        base = &statement.target.operands.front();
    } else if (statement.target.kind != ExpressionKind::Identifier) {
        report(
            target_code,
            std::string { language_name }
                + " force/release supports a signal, static bit-select, or "
                  "static part-select",
            statement.target.span);
        return;
    }
    if (base->kind != ExpressionKind::Identifier) {
        report(
            target_code,
            "nested or runtime-selected " + std::string { language_name }
                + " force/release targets are "
                  "not supported",
            statement.target.span);
        return;
    }

    auto target_name = base->text;
    std::uint32_t offset = 0;
    std::optional<DynamicIndex> dynamic_selection;
    std::optional<std::size_t> selected_width;
    std::optional<frontend::ValueDomain> selected_domain;
    if (!signals_.contains(target_name)) {
        if (const auto selected = packed_member_reference(target_name)) {
            const auto width = selected->member->width();
            if (!width || *width == 0
                || *width > std::numeric_limits<std::uint32_t>::max()
                || selected->lsb_offset
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    visibility_code,
                    "packed force/release member has no executable layout",
                    statement.target.span);
                return;
            }
            target_name = selected->base;
            offset = static_cast<std::uint32_t>(selected->lsb_offset);
            selected_width = static_cast<std::size_t>(*width);
            selected_domain = selected->member->domain;
        }
    }
    const auto signal = signals_.find(target_name);
    if (signal == signals_.end()) {
        report(
            visibility_code,
            locals_.contains(target_name)
                ? "procedural force/release of automatic local variables is not "
                  "supported"
                : "procedural force/release requires a visible packed signal",
            statement.target.span);
        return;
    }
    const auto whole_width = design_.signal_info_[signal->second].width;
    const auto selection_source_width = selected_width.value_or(whole_width);
    if (statement.target.kind == ExpressionKind::Index) {
        const auto index = static_integer_value(statement.target.operands[1]);
        const auto selected = index
            ? select_offset(*base, *index, selection_source_width)
            : std::nullopt;
        if (!index) {
            dynamic_selection = lower_dynamic_index(
                *base,
                statement.target.operands[1],
                selection_source_width,
                offset,
                statement.target.span);
            if (!dynamic_selection) {
                return;
            }
            selected_width = 1;
        } else if (!selected
            || static_cast<std::uint64_t>(offset) + *selected
                > std::numeric_limits<std::uint32_t>::max()) {
            report(
                target_code,
                "procedural force/release bit-select requires a static in-range "
                "index",
                statement.target.span);
            return;
        } else {
            offset += static_cast<std::uint32_t>(*selected);
            selected_width = 1;
        }
    } else if (statement.target.kind == ExpressionKind::Slice) {
        const auto selected = constant_slice_selection(
            statement.target, selection_source_width);
        if (!selected
            || selected->offset
                > std::numeric_limits<std::uint32_t>::max() - offset
            || selected->width
                > std::numeric_limits<std::uint32_t>::max()) {
            report(
                target_code,
                "procedural force/release part-select requires static in-range "
                "bounds and width",
                statement.target.span);
            return;
        }
        offset += static_cast<std::uint32_t>(selected->offset);
        selected_width = selected->width;
    }
    const auto width = selected_width.value_or(whole_width);
    if (!force) {
        process_.operations.emplace_back(ReleaseSignalSlice {
            signal->second,
            dynamic_selection ? 0U : offset,
            static_cast<std::uint32_t>(width),
            dynamic_selection,
            statement.vhdl_force_driving_value });
        return;
    }
    const auto* target_type = offset == 0 && width == whole_width ? object_type(target_name) : nullptr;
    auto value = lower_expression(statement.value, width, target_type);
    if (!value) {
        return;
    }
    if (register_width(*value) != width) {
        *value = resize_register(
            *value, width, is_signed_expression(statement.value));
    }
    const auto domain = selected_domain.value_or(
        target_type != nullptr
            ? target_type->domain
            : design_.signal_info_[signal->second].source_domain);
    if (is_two_state_domain(domain)
        && !is_two_state_domain(register_domain(*value))) {
        report(
            domain_code,
            "force of a two-state target requires an explicit conversion",
            statement.value.span);
        return;
    }
    process_.operations.emplace_back(
        ForceSignalSlice {
            signal->second,
            *value,
            dynamic_selection ? 0U : offset,
            dynamic_selection,
            statement.vhdl_force_driving_value });
}

void Lowerer::lower_concatenated_force_release(
    const Statement& statement)
{
    if (statement.target.operands.empty()) {
        report(
            "FSIM-ELAB-SVCONCAT-001",
            "a concatenated force/release target requires at least one packed "
            "operand",
            statement.target.span);
        return;
    }
    if (statement.kind == StatementKind::Release) {
        for (const auto& target : statement.target.operands) {
            auto child = statement;
            child.target = target;
            lower_force_release(child);
        }
        return;
    }
    std::vector<std::size_t> widths;
    widths.reserve(statement.target.operands.size());
    std::size_t total_width { };
    for (const auto& target : statement.target.operands) {
        const auto width = infer_width(target);
        if (!width || *width == 0
            || *width > std::numeric_limits<std::uint32_t>::max()
            || total_width > std::numeric_limits<std::uint32_t>::max() - *width) {
            report(
                "FSIM-ELAB-SVCONCAT-001",
                "every concatenated force target must have a static nonzero packed "
                "width and the total width must fit SimIR",
                target.span);
            return;
        }
        widths.push_back(*width);
        total_width += *width;
    }
    auto value = lower_expression(statement.value, total_width);
    if (!value) {
        return;
    }
    if (register_width(*value) != total_width) {
        *value = resize_register(
            *value, total_width, is_signed_expression(statement.value));
    }
    std::vector<RegisterId> values(statement.target.operands.size());
    std::uint32_t offset { };
    for (std::size_t reverse = statement.target.operands.size();
        reverse != 0; --reverse) {
        const auto index = reverse - 1;
        values[index] = allocate_register(
            widths[index], register_domain(*value));
        process_.operations.emplace_back(Extract {
            values[index], *value, offset,
            static_cast<std::uint32_t>(widths[index]) });
        offset += static_cast<std::uint32_t>(widths[index]);
    }
    for (std::size_t index = 0;
        index < statement.target.operands.size(); ++index) {
        const auto temporary = "$fsim_force_concat_value_"
            + std::to_string(process_.operations.size()) + "_"
            + std::to_string(index);
        locals_.emplace(temporary, values[index]);
        auto child = statement;
        child.target = statement.target.operands[index];
        child.value = Expression {
            ExpressionKind::Identifier, temporary, { }, statement.value.span
        };
        lower_force_release(child);
        locals_.erase(temporary);
    }
}

namespace {

    [[nodiscard]] std::string procedural_target_key(
        const frontend::Expression& expression)
    {
        std::string result = std::to_string(static_cast<unsigned>(expression.kind));
        result += ':';
        result += expression.text;
        result += '[';
        for (const auto& operand : expression.operands) {
            const auto key = procedural_target_key(operand);
            result += std::to_string(key.size());
            result += ':';
            result += key;
        }
        result += ']';
        return result;
    }

    [[nodiscard]] const frontend::Expression* packed_target_base(
        const frontend::Expression& expression)
    {
        auto* base = &expression;
        while ((base->kind == frontend::ExpressionKind::Index
                   || base->kind == frontend::ExpressionKind::Slice)
            && !base->operands.empty()) {
            base = &base->operands.front();
        }
        return base->kind == frontend::ExpressionKind::Identifier
            ? base
            : nullptr;
    }

    [[nodiscard]] bool packed_target_is_visible(
        const frontend::Expression& expression,
        const std::unordered_map<std::string, SignalId>& signals)
    {
        if (expression.kind == frontend::ExpressionKind::Concatenation) {
            return !expression.operands.empty()
                && std::ranges::all_of(
                    expression.operands,
                    [&](const frontend::Expression& operand) {
                        return packed_target_is_visible(operand, signals);
                    });
        }
        const auto* base = packed_target_base(expression);
        return base != nullptr && signals.contains(base->text);
    }

} // namespace

void Lowerer::prepare_procedural_continuous_assignments(
    const std::vector<Statement>& statements)
{
    const auto visit = [&](const auto& self,
                           const std::vector<Statement>& nodes) -> void {
        for (const auto& statement : nodes) {
            if (statement.kind == StatementKind::ProceduralAssign) {
                ProceduralContinuousAssignment assignment;
                assignment.source = &statement;
                assignment.target = statement.target;
                assignment.value = statement.value;
                assignment.target_key = procedural_target_key(statement.target);
                assignment.valid = packed_target_is_visible(
                    statement.target, signals_);
                const auto index = procedural_continuous_assignments_.size();
                assignment.active_name = process_.name
                    + ".$procedural_assign_" + std::to_string(index)
                    + ".active";
                if (!assignment.valid) {
                    // The ordinary lowering path owns the source diagnostic;
                    // do not publish unreachable implementation state.
                } else if (design_.signals_.size()
                    > std::numeric_limits<SignalId>::max()) {
                    report(
                        "FSIM-ELAB-SVPROCASSIGN-001",
                        "the design has too many signals for a procedural continuous "
                        "assignment driver",
                        statement.span);
                    assignment.valid = false;
                } else {
                    assignment.active = static_cast<SignalId>(design_.signals_.size());
                    frontend::SourceSpan declaration_span = statement.span;
                    SignalInfo info;
                    info.id = assignment.active;
                    info.name = assignment.active_name;
                    info.width = 1;
                    info.type_name = "logic";
                    info.source_domain = frontend::ValueDomain::Logic4;
                    info.declaration_span = std::move(declaration_span);
                    design_.signal_info_.push_back(std::move(info));
                    design_.signals_.push_back(Signal {
                        assignment.active_name,
                        PackedLogic4 { 1, Logic4::zero },
                        ResolutionKind::none,
                        ValueKind::logic4 });
                    design_.signal_by_name_.emplace(
                        assignment.active_name, assignment.active);
                }
                procedural_continuous_assignment_by_statement_.emplace(
                    &statement, index);
                procedural_continuous_assignments_by_target_[assignment.target_key].push_back(index);
                procedural_continuous_assignments_.push_back(
                    std::move(assignment));
            }
            self(self, statement.statements);
            self(self, statement.else_statements);
            for (const auto& alternative : statement.case_alternatives) {
                self(self, alternative.statements);
            }
            self(self, statement.loop_updates);
        }
    };
    visit(visit, statements);
}

void Lowerer::lower_procedural_continuous_assignment(
    const Statement& statement)
{
    const auto key = procedural_target_key(statement.target);
    const auto matches = procedural_continuous_assignments_by_target_.find(key);
    if (matches == procedural_continuous_assignments_by_target_.end()) {
        report(
            "FSIM-ELAB-SVPROCASSIGN-001",
            "procedural assign/deassign requires a visible packed variable target "
            "that is declared outside the process",
            statement.target.span);
        return;
    }
    const auto zero = allocate_register(1, frontend::ValueDomain::Logic4);
    process_.operations.emplace_back(
        LoadConstant { zero, PackedLogic4 { 1, Logic4::zero } });
    for (const auto index : matches->second) {
        const auto& assignment = procedural_continuous_assignments_[index];
        if (assignment.valid) {
            process_.operations.emplace_back(
                WriteBlocking { assignment.active, zero });
        }
    }

    if (statement.kind == StatementKind::Deassign) {
        Statement preserve = statement;
        preserve.kind = StatementKind::Assignment;
        preserve.assignment_kind = AssignmentKind::Blocking;
        preserve.value = statement.target;
        lower_assignment(preserve);
        Statement release = statement;
        release.kind = StatementKind::Release;
        lower_force_release(release);
        return;
    }

    const auto found = procedural_continuous_assignment_by_statement_.find(
        &statement);
    if (found == procedural_continuous_assignment_by_statement_.end()
        || !procedural_continuous_assignments_[found->second].valid) {
        report(
            "FSIM-ELAB-SVPROCASSIGN-001",
            "procedural continuous assignment requires a visible packed variable "
            "target declared outside the process",
            statement.target.span);
        return;
    }
    Statement force = statement;
    force.kind = StatementKind::Force;
    lower_force_release(force);
    const auto one = allocate_register(1, frontend::ValueDomain::Logic4);
    process_.operations.emplace_back(
        LoadConstant { one, PackedLogic4 { 1, Logic4::one } });
    process_.operations.emplace_back(WriteBlocking {
        procedural_continuous_assignments_[found->second].active, one });
}

void Lowerer::materialize_procedural_continuous_assignments()
{
    if (procedural_continuous_assignments_.empty()) {
        return;
    }
    auto extended_signals = signals_;
    for (const auto& assignment : procedural_continuous_assignments_) {
        if (assignment.valid) {
            extended_signals.emplace(
                assignment.active_name, assignment.active);
        }
    }
    for (std::size_t index = 0;
        index < procedural_continuous_assignments_.size(); ++index) {
        const auto& assignment = procedural_continuous_assignments_[index];
        if (!assignment.valid) {
            continue;
        }
        Statement force;
        force.kind = StatementKind::Force;
        force.target = assignment.target;
        force.value = assignment.value;
        force.span = assignment.source->span;
        Statement active;
        active.kind = StatementKind::If;
        active.condition = Expression {
            ExpressionKind::Identifier,
            assignment.active_name,
            { },
            assignment.source->span
        };
        active.statements.push_back(std::move(force));
        active.span = assignment.source->span;
        active.label = "$procedural_assign_" + std::to_string(index);

        Lowerer driver_lowerer(
            design_, extended_signals, read_only_signals_, string_objects_,
            read_only_string_objects_, container_objects_,
            read_only_container_objects_, visible_types_, visible_type_marks_,
            functions_, tasks_, procedures_, scalar_context_, diagnostics_);
        driver_lowerer.set_systemverilog_program_owner(
            systemverilog_program_owner_);
        driver_lowerer.set_vhdl_standard(vhdl_standard_);
        driver_lowerer.set_vhdl_synopsys_numeric_context(
            vhdl_synopsys_signed_visible_,
            vhdl_synopsys_unsigned_visible_);
        auto driver = driver_lowerer.lower_concurrent(
            active, language_, hierarchy_, index);
        const auto process_index = design_.processes_.size()
            + 1 + generated_processes_.size();
        if (process_index > std::numeric_limits<ProcessId>::max()) {
            report(
                "FSIM-ELAB-SVPROCASSIGN-002",
                "the design has too many processes for a procedural continuous "
                "assignment driver",
                assignment.source->span);
            continue;
        }
        driver.id = static_cast<ProcessId>(process_index);
        generated_processes_.push_back(std::move(driver));
    }
}

} // namespace fsim::elaboration
