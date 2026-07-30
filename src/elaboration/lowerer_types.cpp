// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;



    std::optional<std::size_t> Lowerer::infer_width(const Expression& expression) const {
        if (expression.kind == ExpressionKind::BooleanLiteral) {
            return std::size_t{1};
        }
        if (expression.kind == ExpressionKind::IntegerLiteral) {
            return std::size_t{32};
        }
        if (expression.kind == ExpressionKind::Aggregate) {
            return std::nullopt;
        }
        if (language_ == frontend::Language::Vhdl2008
            && expression.kind == ExpressionKind::Call
            && expression.operands.size() == 1
            && (locals_.contains(expression.text)
                || signals_.contains(expression.text)
                || packed_member_reference(expression.text))) {
            return std::size_t{1};
        }
        if (expression.kind == ExpressionKind::Index
            && expression.operands.size() == 2) {
            return std::size_t{1};
        }
        if (expression.kind == ExpressionKind::Slice
            && expression.operands.size() == 3) {
            if (expression.text == "+:"
                || expression.text == "-:") {
                const auto width =
                    constant_index(expression.operands[2]);
                if (width && *width > 0
                    && static_cast<std::uint64_t>(*width)
                        <= std::numeric_limits<std::size_t>::max()) {
                    return static_cast<std::size_t>(*width);
                }
                return std::nullopt;
            }
            const auto left = constant_index(expression.operands[1]);
            const auto right = constant_index(expression.operands[2]);
            if (left && right) {
                const auto width = index_distance(*left, *right) + 1;
                if (width
                    <= std::numeric_limits<std::size_t>::max()) {
                    return static_cast<std::size_t>(width);
                }
            }
            return std::nullopt;
        }
        if (expression.kind == ExpressionKind::Concatenation) {
            std::size_t width = 0;
            for (const auto& operand : expression.operands) {
                const auto operand_width = infer_width(operand);
                if (!operand_width
                    || *operand_width
                        > std::numeric_limits<std::size_t>::max()
                            - width) {
                    return std::nullopt;
                }
                width += *operand_width;
            }
            return width == 0
                ? std::nullopt
                : std::optional<std::size_t>{width};
        }
        if (expression.kind == ExpressionKind::Replication) {
            if (expression.operands.size() < 2) {
                return std::nullopt;
            }
            std::string count_error;
            const auto count = evaluate_constant_expression(
                expression.operands[0], {}, count_error);
            if (!count || *count <= 0) {
                return std::nullopt;
            }
            std::size_t group_width = 0;
            for (std::size_t index = 1;
                 index < expression.operands.size();
                 ++index) {
                const auto operand_width =
                    infer_width(expression.operands[index]);
                if (!operand_width || *operand_width == 0
                    || *operand_width
                        > std::numeric_limits<std::size_t>::max()
                            - group_width) {
                    return std::nullopt;
                }
                group_width += *operand_width;
            }
            const auto repetitions =
                static_cast<std::uint64_t>(*count);
            if (group_width == 0
                || repetitions
                    > std::numeric_limits<std::size_t>::max()
                          / group_width) {
                return std::nullopt;
            }
            return static_cast<std::size_t>(
                repetitions * group_width);
        }
        if (language_ == frontend::Language::Vhdl2008
            && expression.kind == ExpressionKind::Binary
            && expression.text == "&"
            && expression.operands.size() == 2) {
            const auto lhs = infer_width(expression.operands[0]);
            const auto rhs = infer_width(expression.operands[1]);
            if (!lhs || !rhs
                || *rhs
                    > std::numeric_limits<std::size_t>::max()
                        - *lhs) {
                return std::nullopt;
            }
            return *lhs + *rhs;
        }
        if (language_ == frontend::Language::Vhdl2008
            && expression.kind == ExpressionKind::LogicLiteral) {
            return std::size_t{1};
        }
        if (language_ == frontend::Language::Vhdl2008
            && expression.kind == ExpressionKind::StringLiteral
            && expression.text.size() >= 2) {
            return expression.text.size() - 2;
        }
        if (expression.kind == ExpressionKind::LogicLiteral) {
            const auto quote = expression.text.find('\'');
            if (quote != std::string::npos && quote != 0) {
                const auto width = unsigned_decimal(
                    std::string_view{expression.text}.substr(0, quote));
                if (width && *width != 0
                    && *width
                        <= std::numeric_limits<std::size_t>::max()) {
                    return static_cast<std::size_t>(*width);
                }
            }
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "?:"
            && expression.operands.size() == 3) {
            if (const auto width = infer_width(expression.operands[1])) {
                return width;
            }
            return infer_width(expression.operands[2]);
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "$isunknown") {
            return std::size_t{1};
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "$bits") {
            return std::size_t{32};
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && !expression.operands.empty()
            && expression.operands.front().kind
                == ExpressionKind::Identifier) {
            const auto* type = visible_type_mark(
                expression.operands.front().text);
            if (type != nullptr
                && !type->enumeration_literals.empty()) {
                if (expression.text == "'length"
                    || expression.text == "'pos") {
                    return std::size_t{32};
                }
                if (expression.text == "'ascending") {
                    return std::size_t{1};
                }
                if (expression.text == "'left"
                    || expression.text == "'right"
                    || expression.text == "'low"
                    || expression.text == "'high"
                    || expression.text == "'val"
                    || expression.text == "'succ"
                    || expression.text == "'pred"
                    || expression.text == "'leftof"
                    || expression.text == "'rightof") {
                    const auto width = type->width();
                    if (width
                        && *width
                            <= std::numeric_limits<std::size_t>::max()) {
                        return static_cast<std::size_t>(*width);
                    }
                }
            }
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && (expression.text == "'left"
                || expression.text == "'right"
                || expression.text == "'low"
                || expression.text == "'high"
                || expression.text == "'length")) {
            return std::size_t{32};
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && expression.text == "'ascending") {
            return std::size_t{1};
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && expression.text == "'event") {
            return std::size_t{1};
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && expression.text == "'last_value"
            && expression.operands.size() == 1) {
            return infer_width(expression.operands.front());
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && expression.text == "'last_event") {
            return std::size_t{64};
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && expression.text == "'stable") {
            return std::size_t{1};
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && expression.text == "'active") {
            return std::size_t{1};
        }
        if (expression.kind == ExpressionKind::Call
            && (expression.text == "$left"
                || expression.text == "$right"
                || expression.text == "$low"
                || expression.text == "$high"
                || expression.text == "$size"
                || expression.text == "$increment"
                || expression.text == "$dimensions"
                || expression.text == "$unpacked_dimensions")) {
            return std::size_t{32};
        }
        if (expression.kind == ExpressionKind::Call
            && (expression.text == "$onehot"
                || expression.text == "$onehot0")) {
            return std::size_t{1};
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "$countones") {
            return std::size_t{32};
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "$countbits") {
            return std::size_t{32};
        }
        if (expression.kind == ExpressionKind::Call
            && (expression.text == "$urandom"
                || expression.text == "$random"
                || expression.text == "$urandom_range")) {
            return std::size_t{32};
        }
        if (expression.kind == ExpressionKind::Call) {
            if (const auto* function =
                    visible_function(expression.text)) {
                const auto width = function->return_type.width();
                if (width
                    && *width
                        <= std::numeric_limits<std::size_t>::max()) {
                    return static_cast<std::size_t>(*width);
                }
                return std::nullopt;
            }
        }
        if (expression.kind == ExpressionKind::Identifier) {
            if (const auto local = locals_.find(expression.text);
                local != locals_.end()) {
                return register_width(local->second);
            }
            if (const auto found = signals_.find(expression.text); found != signals_.end()) {
                return design_.signal_info_[found->second].width;
            }
            if (const auto selected =
                    packed_member_reference(expression.text)) {
                const auto width = selected->member->width();
                if (width
                    && *width
                        <= std::numeric_limits<std::size_t>::max()) {
                    return static_cast<std::size_t>(*width);
                }
            }
        }
        for (const auto& operand : expression.operands) {
            if (const auto width = infer_width(operand)) {
                return width;
            }
        }
        return std::nullopt;
    }



    std::optional<frontend::PackedRange> Lowerer::expression_range(
        const Expression& expression,
        const std::size_t width) const {
        if (expression.kind == ExpressionKind::Identifier) {
            if (const auto local =
                    local_ranges_.find(expression.text);
                local != local_ranges_.end()
                && local->second) {
                return *local->second;
            }
            if (const auto signal =
                    signals_.find(expression.text);
                signal != signals_.end()) {
                if (const auto* type =
                        visible_type(expression.text);
                    type != nullptr && type->packed_range) {
                    return *type->packed_range;
                }
                if (design_.signal_info_[signal->second]
                        .packed_range) {
                    return *design_
                                .signal_info_[signal->second]
                                .packed_range;
                }
            }
            if (const auto selected =
                    packed_member_reference(expression.text);
                selected
                && selected->member->packed_range) {
                return *selected->member->packed_range;
            }
        }
        if (width == 0
            || width - 1
                > static_cast<std::size_t>(
                    std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
        return frontend::PackedRange{
            static_cast<std::int64_t>(width - 1), 0, true};
    }



    std::optional<std::size_t> Lowerer::select_offset(
        const Expression& expression,
        const std::int64_t index,
        const std::size_t width) const {
        const auto range = expression_range(expression, width);
        if (!range) {
            return std::nullopt;
        }
        const auto lower = std::min(range->left, range->right);
        const auto upper = std::max(range->left, range->right);
        if (index < lower || index > upper) {
            return std::nullopt;
        }
        const auto offset = index_distance(index, range->right);
        if (offset >= width
            || offset
                > std::numeric_limits<std::size_t>::max()) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(offset);
    }



    [[nodiscard]] bool Lowerer::is_signed_expression(
        const Expression& expression) const {
        switch (expression.kind) {
        case ExpressionKind::Identifier:
            if (const auto local =
                    local_signed_.find(expression.text);
                local != local_signed_.end()) {
                return local->second;
            }
            if (const auto signal = signals_.find(expression.text);
                    signal != signals_.end()) {
                const auto* type =
                    visible_type(expression.text);
                return type != nullptr
                    ? type->is_signed
                    : design_.signal_info_[signal->second].is_signed;
            }
            if (const auto selected =
                    packed_member_reference(expression.text)) {
                return selected->member->is_signed;
            }
            return false;
        case ExpressionKind::IntegerLiteral:
            return true;
        case ExpressionKind::BooleanLiteral:
            return false;
        case ExpressionKind::LogicLiteral:
            return expression.text.find("'s") != std::string::npos
                || expression.text.find("'S") != std::string::npos;
        case ExpressionKind::StringLiteral:
        case ExpressionKind::Aggregate:
        case ExpressionKind::Concatenation:
        case ExpressionKind::Replication:
        case ExpressionKind::Invalid:
            return false;
        case ExpressionKind::Index:
            return false;
        case ExpressionKind::Slice:
            return language_ == frontend::Language::Vhdl2008
                && !expression.operands.empty()
                && is_signed_expression(expression.operands[0]);
        case ExpressionKind::Unary:
            if (expression.operands.size() != 1
                || expression.text == "!"
                || expression.text == "&"
                || expression.text == "|"
                || expression.text == "^"
                || expression.text == "~&"
                || expression.text == "~|"
                || expression.text == "~^"
                || expression.text == "^~") {
                return false;
            }
            return is_signed_expression(expression.operands[0]);
        case ExpressionKind::Call:
            if (const auto* function =
                    visible_function(expression.text)) {
                return function->return_type.is_signed;
            }
            if (language_ != frontend::Language::Vhdl2008
                && expression.text == "$random") {
                return true;
            }
            if (language_ == frontend::Language::Vhdl2008
                && !expression.operands.empty()
                && expression.operands.front().kind
                    == ExpressionKind::Identifier) {
                const auto* type = visible_type_mark(
                    expression.operands.front().text);
                if (type != nullptr
                    && !type->enumeration_literals.empty()) {
                    return expression.text == "'pos"
                        || expression.text == "'length";
                }
            }
            if (language_ == frontend::Language::Vhdl2008
                && (expression.text == "'left"
                    || expression.text == "'right"
                    || expression.text == "'low"
                    || expression.text == "'high"
                    || expression.text == "'length")) {
                return true;
            }
            if (language_ == frontend::Language::Vhdl2008
                && (expression.text == "'ascending"
                    || expression.text == "'event")) {
                return false;
            }
            if (language_ == frontend::Language::Vhdl2008
                && expression.text == "'last_value"
                && expression.operands.size() == 1) {
                return is_signed_expression(expression.operands.front());
            }
            if (language_ == frontend::Language::Vhdl2008
                && expression.text == "'last_event") {
                return true;
            }
            if (language_ == frontend::Language::Vhdl2008
                && expression.text == "'stable") {
                return false;
            }
            if (language_ == frontend::Language::Vhdl2008
                && expression.text == "'active") {
                return false;
            }
            if (language_ != frontend::Language::Vhdl2008
                && expression.operands.size() == 1
                && expression.text == "$signed") {
                return true;
            }
            if (language_ != frontend::Language::Vhdl2008
                && expression.operands.size() == 1
                && expression.text == "$unsigned") {
                return false;
            }
            if (language_
                    == frontend::Language::SystemVerilog2017
                && expression.operands.size() == 1
                && (expression.text == "$left"
                    || expression.text == "$right"
                    || expression.text == "$low"
                    || expression.text == "$high"
                    || expression.text == "$size"
                    || expression.text == "$increment"
                    || expression.text == "$dimensions"
                    || expression.text == "$unpacked_dimensions")) {
                return true;
            }
            if (language_
                    == frontend::Language::SystemVerilog2017
                && expression.operands.size() == 2
                && (expression.text == "$left"
                    || expression.text == "$right"
                    || expression.text == "$low"
                    || expression.text == "$high"
                    || expression.text == "$size"
                    || expression.text == "$increment")) {
                return true;
            }
            if (language_
                    == frontend::Language::SystemVerilog2017
                && expression.operands.size() == 1
                && expression.text == "$countones") {
                return true;
            }
            if (language_
                    == frontend::Language::SystemVerilog2017
                && expression.operands.size() >= 2
                && expression.text == "$countbits") {
                return true;
            }
            if (language_ == frontend::Language::Vhdl2008
                && expression.operands.size() == 1
                && (locals_.contains(expression.text)
                    || signals_.contains(expression.text)
                    || packed_member_reference(expression.text))) {
                return false;
            }
            if (expression.text == "?:"
                && expression.operands.size() == 3) {
                return is_signed_expression(expression.operands[1])
                    && is_signed_expression(expression.operands[2]);
            }
            return false;
        case ExpressionKind::Binary:
            if (expression.operands.size() != 2) {
                return false;
            }
            if (expression.text == "<<"
                || expression.text == ">>"
                || expression.text == "<<<"
                || expression.text == ">>>"
                || expression.text == "sll"
                || expression.text == "srl"
                || expression.text == "sla"
                || expression.text == "sra"
                || expression.text == "rol"
                || expression.text == "ror") {
                return is_signed_expression(expression.operands[0]);
            }
            if (expression.text == "=="
                || expression.text == "==="
                || expression.text == "!=="
                || expression.text == "!="
                || expression.text == "="
                || expression.text == "<"
                || expression.text == "<="
                || expression.text == ">"
                || expression.text == ">="
                || expression.text == "&&"
                || expression.text == "||"
                || (language_ == frontend::Language::Vhdl2008
                    && expression.text == "&")) {
                return false;
            }
            return is_signed_expression(expression.operands[0])
                && is_signed_expression(expression.operands[1]);
        }
        return false;
    }



    [[nodiscard]] bool Lowerer::is_integer_expression(
        const Expression& expression) const {
        if (language_ != frontend::Language::Vhdl2008) {
            return false;
        }
        switch (expression.kind) {
        case ExpressionKind::IntegerLiteral:
            return true;
        case ExpressionKind::Identifier:
            if (const auto local = locals_.find(expression.text);
                local != locals_.end()) {
                return register_domain(local->second)
                    == frontend::ValueDomain::Integer;
            }
            if (const auto signal = signals_.find(expression.text);
                signal != signals_.end()) {
                const auto* type =
                    visible_type(expression.text);
                return (type != nullptr
                            ? type->domain
                            : design_.signal_info_[signal->second]
                                  .source_domain)
                    == frontend::ValueDomain::Integer;
            }
            return false;
        case ExpressionKind::Unary:
            return expression.operands.size() == 1
                && (expression.text == "+"
                    || expression.text == "-"
                    || expression.text == "abs")
                && is_integer_expression(
                    expression.operands.front());
        case ExpressionKind::Binary:
            return expression.operands.size() == 2
                && (expression.text == "+"
                    || expression.text == "-"
                    || expression.text == "*"
                    || expression.text == "/"
                    || expression.text == "mod"
                    || expression.text == "rem"
                    || expression.text == "**")
                && is_integer_expression(expression.operands[0])
                && is_integer_expression(expression.operands[1]);
        case ExpressionKind::Call:
            if (const auto* function =
                    visible_function(expression.text)) {
                return function->return_type.domain
                    == frontend::ValueDomain::Integer;
            }
            if (expression.text == "?:"
                && expression.operands.size() == 3) {
                return is_integer_expression(expression.operands[1])
                    && is_integer_expression(expression.operands[2]);
            }
            if (!expression.operands.empty()
                && expression.operands.front().kind
                    == ExpressionKind::Identifier) {
                const auto* type = visible_type_mark(
                    expression.operands.front().text);
                if (type != nullptr
                    && !type->enumeration_literals.empty()) {
                    return expression.text == "'pos"
                        || expression.text == "'length";
                }
            }
            return expression.text == "'left"
                || expression.text == "'right"
                || expression.text == "'low"
                || expression.text == "'high"
                || expression.text == "'length"
                || expression.text == "'last_event";
        case ExpressionKind::BooleanLiteral:
        case ExpressionKind::LogicLiteral:
        case ExpressionKind::StringLiteral:
        case ExpressionKind::Index:
        case ExpressionKind::Slice:
        case ExpressionKind::Aggregate:
        case ExpressionKind::Concatenation:
        case ExpressionKind::Replication:
        case ExpressionKind::Invalid:
            return false;
        }
        return false;
    }



    void Lowerer::collect_identifiers(
        const Expression& expression,
        std::set<std::string>& output) const {
        if (expression.kind == ExpressionKind::Identifier) {
            if (const auto selected =
                    packed_member_reference(expression.text)) {
                output.insert(selected->base);
            } else {
                output.insert(expression.text);
            }
        } else if (
            language_ == frontend::Language::Vhdl2008
            && expression.kind == ExpressionKind::Call
            && expression.operands.size() == 1) {
            if (const auto selected =
                    packed_member_reference(expression.text)) {
                output.insert(selected->base);
            } else if (
                signals_.contains(expression.text)
                || locals_.contains(expression.text)) {
                output.insert(expression.text);
            }
        }
        for (const auto& operand : expression.operands) {
            collect_identifiers(operand, output);
        }
    }



    void Lowerer::collect_statement_identifiers(
        const std::vector<Statement>& statements,
        std::set<std::string>& output) const {
        for (const auto& statement : statements) {
            switch (statement.kind) {
            case StatementKind::Assignment:
                if (statement.vhdl_waveform.empty()) {
                    collect_identifiers(statement.value, output);
                } else {
                    for (const auto& element :
                         statement.vhdl_waveform) {
                        collect_identifiers(element.value, output);
                    }
                }
                break;
            case StatementKind::If:
            case StatementKind::Assert:
            case StatementKind::WaitUntil:
                collect_identifiers(statement.condition, output);
                break;
            case StatementKind::Return:
                collect_identifiers(statement.value, output);
                break;
            case StatementKind::TaskCall:
                for (const auto& argument :
                     statement.task_arguments) {
                    collect_identifiers(argument, output);
                }
                break;
            case StatementKind::ProcedureCall:
                for (const auto& association :
                     statement.procedure_arguments) {
                    collect_identifiers(
                        association.value, output);
                }
                break;
            case StatementKind::Case:
                collect_identifiers(statement.condition, output);
                for (const auto& alternative :
                     statement.case_alternatives) {
                    for (const auto& choice : alternative.choices) {
                        collect_identifiers(choice, output);
                    }
                }
                break;
            case StatementKind::Loop:
                if (statement.loop_runtime) {
                    collect_identifiers(
                        statement.condition, output);
                } else {
                    collect_identifiers(
                        statement.loop_initial, output);
                    collect_identifiers(
                        statement.loop_limit, output);
                }
                break;
            case StatementKind::Break:
            case StatementKind::Continue:
            case StatementKind::Delay:
            case StatementKind::WaitOn:
            case StatementKind::Display:
                if (statement.output_format) {
                    collect_identifiers(statement.value, output);
                }
                for (const auto& value : statement.output_values) {
                    collect_identifiers(value.value, output);
                }
                break;
            case StatementKind::MonitorControl:
                break;
            case StatementKind::EventTrigger:
            case StatementKind::Report:
            case StatementKind::Pause:
            case StatementKind::Finish:
            case StatementKind::Block:
            case StatementKind::Null:
                break;
            }
            collect_statement_identifiers(
                statement.statements, output);
            collect_statement_identifiers(
                statement.else_statements, output);
            for (const auto& alternative :
                 statement.case_alternatives) {
                collect_statement_identifiers(
                    alternative.statements, output);
            }
        }
    }



    RegisterId Lowerer::allocate_register(
        const std::size_t width,
        const frontend::ValueDomain domain) {
        const auto id = next_register_++;
        register_widths_.push_back(width);
        register_domains_.push_back(domain);
        return id;
    }



    [[nodiscard]] std::size_t Lowerer::register_width(const RegisterId id) const {
        return register_widths_.at(static_cast<std::size_t>(id));
    }



    [[nodiscard]] frontend::ValueDomain Lowerer::register_domain(
        const RegisterId id) const {
        return register_domains_.at(static_cast<std::size_t>(id));
    }



    [[nodiscard]] RegisterId Lowerer::resize_register(
        const RegisterId source,
        const std::size_t width,
        const bool sign_extend) {
        const auto source_width = register_width(source);
        if (source_width == width) {
            return source;
        }
        const auto domain = register_domain(source);
        const auto destination =
            allocate_register(width, domain);
        if (width < source_width) {
            process_.operations.emplace_back(Extract{
                destination,
                source,
                0,
                static_cast<std::uint32_t>(width)});
            return destination;
        }
        const auto extension_width = width - source_width;
        RegisterId extension{};
        if (sign_extend) {
            extension = allocate_register(1, domain);
            process_.operations.emplace_back(Extract{
                extension,
                source,
                static_cast<std::uint32_t>(source_width - 1U),
                1});
        } else {
            extension = allocate_register(
                extension_width,
                frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant{
                extension,
                unsigned_value(0, extension_width)});
        }
        std::vector<RegisterId> operands;
        if (sign_extend) {
            operands.assign(extension_width, extension);
        } else {
            operands.push_back(extension);
        }
        operands.push_back(source);
        process_.operations.emplace_back(Concatenate{
            destination,
            std::move(operands),
            static_cast<std::uint32_t>(width)});
        return destination;
    }



    void Lowerer::report(std::string code, std::string message, frontend::SourceSpan span) {
        diagnostics_.push_back({std::move(code), std::move(message), std::move(span)});
    }

} // namespace fsim::elaboration
