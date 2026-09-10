// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

std::optional<std::size_t> Lowerer::infer_width(const Expression& expression) const
{
    if (language_ == frontend::Language::Vhdl2008
        && expression.kind == ExpressionKind::Call) {
        const auto separator = expression.text.find_last_of('.');
        const auto name = std::string_view { expression.text }.substr(
            separator == std::string::npos ? 0 : separator + 1);
        if (name == "conv_integer") {
            return static_cast<std::size_t>(
                frontend::vhdl_predefined_integer_storage_width(
                    vhdl_standard_));
        }
        if ((name == "and_reduce" || name == "nand_reduce"
                || name == "or_reduce" || name == "nor_reduce"
                || name == "xor_reduce" || name == "xnor_reduce")) {
            return std::size_t { 1 };
        }
        if ((name == "conv_signed" || name == "conv_unsigned"
                || name == "conv_std_logic_vector" || name == "ext"
                || name == "sxt")
            && expression.operands.size() == 2U) {
            std::string error;
            const auto width = evaluate_constant_expression(
                expression.operands[1], { }, error);
            if (width && *width >= 0
                && static_cast<std::uint64_t>(*width)
                    <= std::numeric_limits<std::uint32_t>::max()) {
                return static_cast<std::size_t>(*width);
            }
        }
        if ((name == "shl" || name == "shr")
            && !expression.operands.empty()) {
            return infer_width(expression.operands.front());
        }
    }
    if (expression.call_result_width != 0
        && expression.call_result_width
            <= std::numeric_limits<std::size_t>::max()) {
        return static_cast<std::size_t>(expression.call_result_width);
    }
    if (expression.kind == ExpressionKind::Update
        && expression.operands.size() == 1) {
        return infer_width(expression.operands.front());
    }
    if (expression.kind == ExpressionKind::BooleanLiteral) {
        return std::size_t { 1 };
    }
    if (expression.kind == ExpressionKind::IntegerLiteral) {
        return language_ == frontend::Language::Vhdl2008
            ? static_cast<std::size_t>(
                  frontend::vhdl_predefined_integer_storage_width(
                      vhdl_standard_))
            : std::size_t { 32 };
    }
    if (expression.kind == ExpressionKind::Aggregate) {
        return std::nullopt;
    }
    if ((expression.kind == ExpressionKind::Call
            || expression.kind == ExpressionKind::Index
            || expression.kind == ExpressionKind::Slice)
        && language_ == frontend::Language::Vhdl2008) {
        if (const auto type = vhdl_expression_type(expression)) {
            const auto width = type->width();
            if (width && *width <= std::numeric_limits<std::size_t>::max()) {
                return static_cast<std::size_t>(*width);
            }
        }
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "inside") {
        return std::size_t { 1 };
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "@stream-left"
            || expression.text == "@stream-right")) {
        if (expression.operands.size() < 2) {
            return std::nullopt;
        }
        std::size_t width = 0;
        for (std::size_t index = 1;
            index < expression.operands.size(); ++index) {
            const auto operand_width = infer_width(expression.operands[index]);
            if (!operand_width || *operand_width == 0
                || *operand_width
                    > std::numeric_limits<std::size_t>::max()
                        - width) {
                return std::nullopt;
            }
            width += *operand_width;
        }
        return width == 0
            ? std::nullopt
            : std::optional<std::size_t> { width };
    }
    if (expression.kind == ExpressionKind::Call
        && expression.operands.size() == 1
        && (expression.text == ".sum"
            || expression.text == ".product"
            || expression.text == ".and"
            || expression.text == ".or"
            || expression.text == ".xor")
        && expression.operands.front().kind
            == ExpressionKind::Identifier) {
        const auto* type = object_type(expression.operands.front().text);
        return type != nullptr
                && type->systemverilog_container
            ? type->width()
            : std::nullopt;
    }
    if (language_ == frontend::Language::Vhdl2008
        && expression.kind == ExpressionKind::Call
        && expression.operands.size() == 1
        && (locals_.contains(expression.text)
            || signals_.contains(expression.text)
            || packed_member_reference(expression.text))) {
        return std::size_t { 1 };
    }
    if (expression.kind == ExpressionKind::Index
        && expression.operands.size() == 2) {
        if (is_string_expression(expression.operands.front())) {
            return std::size_t { 8 };
        }
        if (const auto* type = container_expression_type(
                expression.operands.front())) {
            return type->width();
        }
        return std::size_t { 1 };
    }
    if (expression.kind == ExpressionKind::Slice
        && expression.operands.size() == 3) {
        if (expression.text == "+:"
            || expression.text == "-:") {
            const auto width = constant_index(expression.operands[2]);
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
            if (operand.kind == ExpressionKind::Replication
                && !operand.operands.empty()) {
                std::string count_error;
                const auto count = evaluate_constant_expression(
                    operand.operands.front(), { }, count_error);
                if (count && *count == 0) {
                    continue;
                }
            }
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
            : std::optional<std::size_t> { width };
    }
    if (expression.kind == ExpressionKind::Replication) {
        if (expression.operands.size() < 2) {
            return std::nullopt;
        }
        std::string count_error;
        const auto count = evaluate_constant_expression(
            expression.operands[0], { }, count_error);
        if (!count || *count <= 0) {
            return std::nullopt;
        }
        std::size_t group_width = 0;
        for (std::size_t index = 1;
            index < expression.operands.size();
            ++index) {
            const auto operand_width = infer_width(expression.operands[index]);
            if (!operand_width || *operand_width == 0
                || *operand_width
                    > std::numeric_limits<std::size_t>::max()
                        - group_width) {
                return std::nullopt;
            }
            group_width += *operand_width;
        }
        const auto repetitions = static_cast<std::uint64_t>(*count);
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
        return std::size_t { 1 };
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
                std::string_view { expression.text }.substr(0, quote));
            if (width && *width != 0
                && *width
                    <= std::numeric_limits<std::size_t>::max()) {
                return static_cast<std::size_t>(*width);
            }
        }
    }
    if ((expression.kind == ExpressionKind::Conditional
         || (expression.kind == ExpressionKind::Call
             && expression.text == "?:"))
        && expression.operands.size() == 3) {
        const auto when_true = infer_width(expression.operands[1]);
        const auto when_false = infer_width(expression.operands[2]);
        if (when_true && when_false) {
            return std::max(*when_true, *when_false);
        }
        return when_true ? when_true : when_false;
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == ".len"
            || expression.text == ".compare"
            || expression.text == ".icompare"
            || expression.text == ".getc"
            || expression.text == ".atoi"
            || expression.text == ".atohex"
            || expression.text == ".atooct"
            || expression.text == ".atobin"
            || expression.text == ".atoreal")) {
        return expression.text == ".atoreal"
            ? std::size_t { 64 }
            : std::size_t { 32 };
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$isunknown") {
        return std::size_t { 1 };
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$sampled"
            || expression.text == "$past"
            || expression.text == "$past_gclk"
            || expression.text == "$future_gclk")
        && !expression.operands.empty()) {
        return infer_width(expression.operands.front());
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$rose"
            || expression.text == "$fell"
            || expression.text == "$stable"
            || expression.text == "$changed"
            || expression.text == "$rose_gclk"
            || expression.text == "$fell_gclk"
            || expression.text == "$stable_gclk"
            || expression.text == "$changed_gclk"
            || expression.text == "$rising_gclk"
            || expression.text == "$falling_gclk"
            || expression.text == "$steady_gclk"
            || expression.text == "$changing_gclk")) {
        return std::size_t { 1 };
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$bits") {
        return std::size_t { 32 };
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$time"
            || expression.text == "$realtime")) {
        return std::size_t { 64 };
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$stime"
            || expression.text == "$coverage_control"
            || expression.text == "$coverage_get"
            || expression.text == "$coverage_get_max"
            || expression.text == "$coverage_merge"
            || expression.text == "$coverage_save")) {
        return std::size_t { 32 };
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$get_coverage"
            || expression.text == "$get_inst_coverage")) {
        return std::size_t { 64 };
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008) {
        const auto separator = expression.text.find_last_of('.');
        const auto name = std::string_view { expression.text }.substr(
            separator == std::string::npos ? 0 : separator + 1);
        if (name == "to_integer") {
            return static_cast<std::size_t>(
                frontend::vhdl_predefined_integer_storage_width(
                    vhdl_standard_));
        }
        if (name == "is_x") {
            return std::size_t { 1 };
        }
        if (name == "vitalextendtofilldelay") {
            return std::size_t { 384 };
        }
        if (name == "vitalcalcdelay") {
            return std::size_t { 64 };
        }
        if (name == "vitalbuf" || name == "vitalinv"
            || name == "vitalident" || name == "vitalbufif0"
            || name == "vitalbufif1" || name == "vitalinvif0"
            || name == "vitalinvif1" || name == "vitaland"
            || name == "vitalor" || name == "vitalxor"
            || name == "vitalnand" || name == "vitalnor"
            || name == "vitalxnor" || name == "vitaland2"
            || name == "vitalor2" || name == "vitalxor2"
            || name == "vitalnand2" || name == "vitalnor2"
            || name == "vitalxnor2" || name == "vitaland3"
            || name == "vitalor3" || name == "vitalxor3"
            || name == "vitalnand3" || name == "vitalnor3"
            || name == "vitalxnor3" || name == "vitaland4"
            || name == "vitalor4" || name == "vitalxor4"
            || name == "vitalnand4" || name == "vitalnor4"
            || name == "vitalxnor4" || name == "vitalmux"
            || name == "vitalmux2" || name == "vitalmux4"
            || name == "vitalmux8") {
            return std::size_t { 1 };
        }
        if ((name == "to_ufixed" || name == "to_sfixed"
                || name == "resize")
            && expression.operands.size() == 3) {
            std::string left_error;
            std::string right_error;
            const auto left = evaluate_constant_expression(
                expression.operands[1], { }, left_error);
            const auto right = evaluate_constant_expression(
                expression.operands[2], { }, right_error);
            if (left && right && *left >= *right) {
                const auto distance = index_distance(*left, *right);
                if (distance
                    < std::numeric_limits<std::uint32_t>::max()) {
                    return static_cast<std::size_t>(distance + 1U);
                }
            }
        }
        if ((name == "to_signed" || name == "to_unsigned"
                || name == "resize")
            && expression.operands.size() == 2) {
            std::string error;
            const auto width = evaluate_constant_expression(
                expression.operands[1], { }, error);
            if (width && *width > 0
                && static_cast<std::uint64_t>(*width)
                    <= std::numeric_limits<std::uint32_t>::max()) {
                return static_cast<std::size_t>(*width);
            }
        }
        if ((name == "shift_left" || name == "shift_right"
                || name == "rotate_left" || name == "rotate_right")
            && !expression.operands.empty()) {
            return infer_width(expression.operands.front());
        }
        if ((name == "to_bit" || name == "to_bitvector"
                || name == "to_bit_vector" || name == "to_bv"
                || name == "to_stdulogic"
                || name == "to_stdlogicvector"
                || name == "to_std_logic_vector" || name == "to_slv"
                || name == "to_stdulogicvector"
                || name == "to_std_ulogic_vector" || name == "to_sulv"
                || name == "to_01" || name == "to_x01"
                || name == "to_x01z" || name == "to_ux01")
            && !expression.operands.empty()) {
            return infer_width(expression.operands.front());
        }
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008
        && !expression.operands.empty()
        && expression.operands.front().kind
            == ExpressionKind::Identifier) {
        const auto* type = visible_type_mark(
            expression.operands.front().text);
        if (type == nullptr
            && vhdl_standard_ >= frontend::VhdlStandard::Vhdl2019) {
            type = object_type(expression.operands.front().text);
        }
        if (type != nullptr
            && !type->enumeration_literals.empty()) {
            if (expression.text == "'length"
                || expression.text == "'pos") {
                return static_cast<std::size_t>(
                    frontend::vhdl_predefined_integer_storage_width(
                        vhdl_standard_));
            }
            if (expression.text == "'ascending") {
                return std::size_t { 1 };
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
        return static_cast<std::size_t>(
            frontend::vhdl_predefined_integer_storage_width(
                vhdl_standard_));
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008
        && expression.text == "'ascending") {
        return std::size_t { 1 };
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008
        && expression.text == "'event") {
        return std::size_t { 1 };
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008
        && (expression.text == "'last_value"
            || expression.text == "'driving_value"
            || expression.text == "'delayed")
        && !expression.operands.empty()) {
        return infer_width(expression.operands.front());
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008
        && (expression.text == "'last_event"
            || expression.text == "'last_active")) {
        return std::size_t { 64 };
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008
        && expression.text == "'stable") {
        return std::size_t { 1 };
    }
    if (expression.kind == ExpressionKind::Call
        && language_ == frontend::Language::Vhdl2008
        && (expression.text == "'active"
            || expression.text == "'driving"
            || expression.text == "'quiet"
            || expression.text == "'transaction")) {
        return std::size_t { 1 };
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
        return std::size_t { 32 };
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$onehot"
            || expression.text == "$onehot0")) {
        return std::size_t { 1 };
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$countones") {
        return std::size_t { 32 };
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$countbits") {
        return std::size_t { 32 };
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$urandom"
            || expression.text == "$random"
            || expression.text == "$urandom_range"
            || expression.text == "std::randomize")) {
        return std::size_t { 32 };
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$test$plusargs"
            || expression.text == "$value$plusargs")) {
        return std::size_t { 32 };
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$fopen"
            || expression.text == "$fgets"
            || expression.text == "$fgetc"
            || expression.text == "$ungetc"
            || expression.text == "$feof"
            || expression.text == "$ferror"
            || expression.text == "$fscanf"
            || expression.text == "$sscanf"
            || expression.text == "$fread"
            || expression.text == "$fseek"
            || expression.text == "$ftell"
            || expression.text == "$rewind")) {
        return std::size_t { 32 };
    }
    if (expression.kind == ExpressionKind::Call) {
        if (const auto* function = visible_function(expression.text)) {
            if (function->return_type.systemverilog_container) {
                return std::nullopt;
            }
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
        constexpr std::string_view triggered_suffix { ".triggered" };
        if (language_ == frontend::Language::SystemVerilog2017
            && expression.text.ends_with(triggered_suffix)) {
            const auto event_name = expression.text.substr(
                0, expression.text.size() - triggered_suffix.size());
            if (const auto found = signals_.find(event_name);
                found != signals_.end()
                && design_.signal_info_[found->second].type_name
                    == "event") {
                return std::size_t { 1 };
            }
        }
        if (const auto local = locals_.find(expression.text);
            local != locals_.end()) {
            return register_width(local->second);
        }
        if (const auto found = signals_.find(expression.text); found != signals_.end()) {
            return design_.signal_info_[found->second].width;
        }
        if (const auto selected = packed_member_reference(expression.text)) {
            const auto width = selected->member->width();
            if (width
                && *width
                    <= std::numeric_limits<std::size_t>::max()) {
                return static_cast<std::size_t>(*width);
            }
        }
        if (const auto* type = visible_type_mark(expression.text)) {
            const auto width = type->width();
            if (width
                && *width <= std::numeric_limits<std::size_t>::max()) {
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
    const std::size_t width) const
{
    if (expression.kind == ExpressionKind::Identifier) {
        if (const auto local = local_ranges_.find(expression.text);
            local != local_ranges_.end()
            && local->second) {
            return *local->second;
        }
        if (const auto signal = signals_.find(expression.text);
            signal != signals_.end()) {
            if (const auto* type = visible_type(expression.text);
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
        if (const auto selected = packed_member_reference(expression.text);
            selected
            && selected->member->packed_range) {
            return *selected->member->packed_range;
        }
        if (const auto* type = visible_type_mark(expression.text);
            type != nullptr && type->packed_range) {
            return *type->packed_range;
        }
    }
    if (language_ == frontend::Language::Vhdl2008) {
        if (const auto type = vhdl_expression_type(expression);
            type && type->packed_range) {
            return *type->packed_range;
        }
    }
    if (width == 0
        || width - 1
            > static_cast<std::size_t>(
                std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return frontend::PackedRange {
        static_cast<std::int64_t>(width - 1), 0, true
    };
}

std::optional<std::size_t> Lowerer::select_offset(
    const Expression& expression,
    const std::int64_t index,
    const std::size_t width) const
{
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
    const Expression& expression) const
{
    switch (expression.kind) {
    case ExpressionKind::Identifier:
        if (const auto local = local_signed_.find(expression.text);
            local != local_signed_.end()) {
            return local->second;
        }
        if (const auto signal = signals_.find(expression.text);
            signal != signals_.end()) {
            const auto* type = visible_type(expression.text);
            if (type != nullptr) {
                return type->is_signed
                    || (vhdl_synopsys_signed_visible_
                        && is_synopsys_std_logic_vector_expression(expression));
            }
            return design_.signal_info_[signal->second].is_signed;
        }
        if (const auto selected = packed_member_reference(expression.text)) {
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
    case ExpressionKind::DefaultChoice:
    case ExpressionKind::Invalid:
        return false;
    case ExpressionKind::Conditional:
        return expression.operands.size() == 3U
            && is_signed_expression(expression.operands[1])
            && is_signed_expression(expression.operands[2]);
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
    case ExpressionKind::Update:
        return expression.operands.size() == 1
            && is_signed_expression(expression.operands.front());
    case ExpressionKind::Call:
        if (language_ == frontend::Language::Vhdl2008) {
            const auto separator = expression.text.find_last_of('.');
            const auto name = std::string_view { expression.text }.substr(
                separator == std::string::npos ? 0 : separator + 1);
            if (name == "conv_signed" || name == "sxt"
                || name == "conv_integer") {
                return true;
            }
            if (name == "conv_unsigned" || name == "conv_std_logic_vector"
                || name == "ext") {
                return false;
            }
            if ((name == "shl" || name == "shr")
                && !expression.operands.empty()) {
                return is_signed_expression(expression.operands.front());
            }
        }
        if (expression.call_result_width != 0) {
            return expression.call_result_signed;
        }
        if (language_ == frontend::Language::Vhdl2008
            && expression.text.starts_with(
                "@vhdl-physical:")) {
            return true;
        }
        if (const auto separator = expression.text.find_last_of('.');
            separator != std::string::npos) {
            const auto object = visible_types_.find(
                expression.text.substr(0, separator));
            const auto method = expression.text.substr(separator + 1);
            if (object != visible_types_.end()
                && object->second != nullptr
                && object->second->vhdl_protected
                && std::ranges::any_of(
                    object->second->vhdl_protected->functions,
                    [&](const auto& function) {
                        return function.name == method
                            && function.return_type.domain
                            == frontend::ValueDomain::Integer;
                    })) {
                return true;
            }
        }
        if (const auto type = vhdl_expression_type(expression);
            type
            && type->domain
                == frontend::ValueDomain::Integer) {
            return true;
        }
        if (expression.text == "@vhdl-dereference") {
            const auto type = vhdl_expression_type(expression);
            return type && type->is_signed;
        }
        if (language_ == frontend::Language::Vhdl2008) {
            const auto separator = expression.text.find_last_of('.');
            const auto name = std::string_view { expression.text }.substr(
                separator == std::string::npos ? 0 : separator + 1);
            if (name == "to_signed" || name == "to_integer") {
                return true;
            }
            if (name == "to_unsigned") {
                return false;
            }
            if ((name == "resize" || name == "shift_left"
                    || name == "shift_right" || name == "rotate_left"
                    || name == "rotate_right")
                && !expression.operands.empty()) {
                return is_signed_expression(expression.operands.front());
            }
        }
        if (expression.operands.size() == 1
            && (expression.text == ".sum"
                || expression.text == ".product"
                || expression.text == ".and"
                || expression.text == ".or"
                || expression.text == ".xor")
            && expression.operands.front().kind
                == ExpressionKind::Identifier) {
            const auto* type = object_type(expression.operands.front().text);
            if (type != nullptr
                && type->systemverilog_container) {
                return type->is_signed;
            }
        }
        if (const auto* function = visible_function(expression.text)) {
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
            if (type == nullptr
                && vhdl_standard_ >= frontend::VhdlStandard::Vhdl2019) {
                type = object_type(expression.operands.front().text);
            }
            if (type != nullptr) {
                if (!type->enumeration_literals.empty()) {
                    return expression.text == "'pos"
                        || expression.text == "'length";
                }
                if (type->domain
                    == frontend::ValueDomain::Integer) {
                    return expression.text != "'ascending"
                        && expression.text != "'range"
                        && expression.text != "'reverse_range";
                }
                if (type->packed_members.empty()
                    && !is_vhdl_array_like(*type)
                    && (type->domain == frontend::ValueDomain::Boolean
                        || type->domain
                            == frontend::ValueDomain::Bit2)) {
                    return expression.text == "'pos"
                        || expression.text == "'length";
                }
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
            && (expression.text == "'last_value"
                || expression.text == "'driving_value"
                || expression.text == "'delayed")
            && !expression.operands.empty()) {
            return is_signed_expression(expression.operands.front());
        }
        if (language_ == frontend::Language::Vhdl2008
            && (expression.text == "'last_event"
                || expression.text == "'last_active")) {
            return true;
        }
        if (language_ == frontend::Language::Vhdl2008
            && expression.text == "'stable") {
            return false;
        }
        if (language_ == frontend::Language::Vhdl2008
            && (expression.text == "'active"
                || expression.text == "'driving"
                || expression.text == "'quiet"
                || expression.text == "'transaction")) {
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
            && (expression.text == "$fopen"
                || expression.text == "$fgets"
                || expression.text == "$fgetc"
                || expression.text == "$ungetc"
                || expression.text == "$feof"
                || expression.text == "$ferror"
                || expression.text == "$fscanf"
                || expression.text == "$sscanf"
                || expression.text == "$fread"
                || expression.text == "$fseek"
                || expression.text == "$ftell"
                || expression.text == "$rewind")) {
            return true;
        }
        if (language_ != frontend::Language::Vhdl2008
            && (expression.text == "$test$plusargs"
                || expression.text == "$value$plusargs")) {
            return true;
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
            || expression.text == "?="
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

[[nodiscard]] bool Lowerer::is_synopsys_std_logic_vector_expression(
    const Expression& expression) const
{
    const auto type = vhdl_expression_type(expression);
    if (!type) {
        return false;
    }
    const auto separator = type->spelling.find_last_of('.');
    const auto name = std::string_view { type->spelling }.substr(
        separator == std::string::npos ? 0 : separator + 1);
    return name == "std_logic_vector";
}

[[nodiscard]] bool Lowerer::is_integer_expression(
    const Expression& expression) const
{
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
            const auto* type = visible_type(expression.text);
            return (type != nullptr
                           ? type->domain
                           : design_.signal_info_[signal->second]
                                 .source_domain)
                == frontend::ValueDomain::Integer;
        }
        if (vhdl_standard_ >= frontend::VhdlStandard::Vhdl2019) {
            const auto separator = expression.text.find_last_of('.');
            if (separator != std::string::npos) {
                const auto receiver = expression.text.substr(0U, separator);
                const auto method = std::string_view { expression.text }.substr(
                    separator + 1U);
                const auto* type = object_type(receiver);
                const auto type_name = type == nullptr ? std::string_view { }
                    : !type->vhdl_type_declaration.empty()
                        ? std::string_view { type->vhdl_type_declaration }
                        : std::string_view { type->spelling };
                const bool mirror = type != nullptr
                    && (type_name.find("value_mirror") != std::string_view::npos
                        || type_name.find("subtype_mirror")
                            != std::string_view::npos);
                if (mirror && (method == "pos" || method == "length"
                        || method == "units_length"
                        || method == "unit_index" || method == "scale"
                        || method == "element_index"
                        || method == "dimensions"
                        || (method == "value"
                            && (type_name.find("integer_value_mirror")
                                    != std::string_view::npos
                                || type_name.find("physical_value_mirror")
                                    != std::string_view::npos)))) {
                    return true;
                }
            }
        }
        return false;
    case ExpressionKind::Unary:
        return expression.operands.size() == 1
            && (expression.text == "+"
                || expression.text == "-"
                || expression.text == "abs")
            && is_integer_expression(
                expression.operands.front());
    case ExpressionKind::Update:
        return false;
    case ExpressionKind::Binary:
        if (const auto found = function_indices_.find(expression.text);
            found != function_indices_.end()
            && expression.operands.size() == 2) {
            const Expression call {
                ExpressionKind::Call,
                expression.text,
                expression.operands,
                expression.span
            };
            if (std::ranges::any_of(
                    found->second,
                    [&](const std::size_t index) {
                        const auto& function = *function_frames_[index].source;
                        return function.return_type.domain
                            == frontend::ValueDomain::Integer
                            && vhdl_function_profile_matches(
                                call, function, nullptr);
                    })) {
                return true;
            }
        }
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
        if (language_ == frontend::Language::Vhdl2008) {
            const auto separator = expression.text.find_last_of('.');
            const auto name = std::string_view { expression.text }.substr(
                separator == std::string::npos ? 0 : separator + 1);
            if (name == "to_integer" || name == "conv_integer") {
                return true;
            }
            if (name == "vitalcalcdelay") {
                return true;
            }
        }
        if (language_ == frontend::Language::Vhdl2008
            && expression.text.starts_with(
                "@vhdl-physical:")) {
            return true;
        }
        if (const auto type = vhdl_expression_type(expression);
            type && type->domain == frontend::ValueDomain::Integer) {
            return true;
        }
        if (expression.text == "@vhdl-dereference") {
            const auto type = vhdl_expression_type(expression);
            return type
                && type->domain
                == frontend::ValueDomain::Integer;
        }
        if (expression.operands.size() == 1) {
            constexpr std::string_view qualification_prefix {
                "@vhdl-qualified:"
            };
            auto type_name = std::string_view { expression.text };
            if (type_name.starts_with(qualification_prefix)) {
                type_name.remove_prefix(qualification_prefix.size());
            }
            if (const auto* type = visible_type_mark(type_name)) {
                if (type->domain == frontend::ValueDomain::Integer) {
                    return true;
                }
            } else if (
                type_name == "integer" || type_name == "natural"
                || type_name == "positive") {
                return true;
            }
        }
        if (const auto* function = visible_function(expression.text)) {
            return function->return_type.domain
                == frontend::ValueDomain::Integer;
        }
        if (const auto found = function_indices_.find(expression.text);
            found != function_indices_.end()) {
            return std::ranges::any_of(
                found->second,
                [&](const std::size_t index) {
                    return function_frames_[index]
                               .source->return_type.domain
                        == frontend::ValueDomain::Integer;
                });
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
            if (type == nullptr
                && vhdl_standard_ >= frontend::VhdlStandard::Vhdl2019) {
                type = object_type(expression.operands.front().text);
            }
            if (type != nullptr) {
                if (!type->enumeration_literals.empty()) {
                    return expression.text == "'pos"
                        || expression.text == "'length";
                }
                if (type->domain
                    == frontend::ValueDomain::Integer) {
                    return expression.text != "'ascending"
                        && expression.text != "'range"
                        && expression.text != "'reverse_range";
                }
                if (type->packed_members.empty()
                    && !is_vhdl_array_like(*type)
                    && (type->domain == frontend::ValueDomain::Boolean
                        || type->domain
                            == frontend::ValueDomain::Bit2)) {
                    return expression.text == "'pos"
                        || expression.text == "'length";
                }
            }
        }
        return expression.text == "'left"
            || expression.text == "'right"
            || expression.text == "'low"
            || expression.text == "'high"
            || expression.text == "'length"
            || expression.text == "'last_event"
            || expression.text == "'last_active";
    case ExpressionKind::BooleanLiteral:
    case ExpressionKind::LogicLiteral:
    case ExpressionKind::StringLiteral:
    case ExpressionKind::Index:
    case ExpressionKind::Slice:
    case ExpressionKind::Aggregate:
    case ExpressionKind::Concatenation:
    case ExpressionKind::Replication:
    case ExpressionKind::DefaultChoice:
    case ExpressionKind::Invalid:
        return false;
    case ExpressionKind::Conditional:
        return expression.operands.size() == 3U
            && is_integer_expression(expression.operands[1])
            && is_integer_expression(expression.operands[2]);
    }
    return false;
}

} // namespace fsim::elaboration
