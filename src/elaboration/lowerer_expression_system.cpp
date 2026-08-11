// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

Lowerer::ExpressionAttempt Lowerer::lower_system_function_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type)
{
    if (auto floating = lower_vhdl_float_function_expression(
            expression, expected_width, expected_type);
        floating.handled) {
        return floating;
    }
    if (auto logic = lower_vhdl_logic_function_expression(
            expression, expected_width, expected_type);
        logic.handled) {
        return logic;
    }
    if (auto fixed = lower_vhdl_fixed_function_expression(
            expression, expected_width, expected_type);
        fixed.handled) {
        return fixed;
    }
    if (auto numeric = lower_vhdl_numeric_function_expression(
            expression, expected_width, expected_type);
        numeric.handled) {
        return numeric;
    }
    if (auto binary = lower_file_binary_read(expression); binary.handled) {
        return binary;
    }
    if (auto scan = lower_file_scan(expression); scan.handled) {
        return scan;
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "@stream-left"
            || expression.text == "@stream-right")) {
        if (language_
                != frontend::Language::SystemVerilog2017
            || expression.operands.size() < 2) {
            report(
                "FSIM-ELAB-SVEXPR-001",
                "streaming concatenation requires SystemVerilog and "
                "at least one packed operand",
                expression.span);
            return std::nullopt;
        }
        const auto slice_size = static_integer_value(
            expression.operands.front());
        if (!slice_size || *slice_size <= 0
            || *slice_size > 64) {
            report(
                "FSIM-ELAB-SVEXPR-002",
                "streaming concatenation requires a positive locally "
                "constant slice size within the 64-bit integral "
                "contract",
                expression.operands.front().span);
            return std::nullopt;
        }
        std::vector<Expression> stream_operands;
        stream_operands.reserve(expression.operands.size() - 1);
        for (std::size_t index = 1;
            index < expression.operands.size(); ++index) {
            if (is_container_expression(
                    expression.operands[index])) {
                report(
                    "FSIM-ELAB-SVEXPR-003",
                    "streaming concatenation supports only fixed-width "
                    "packed integral operands",
                    expression.operands[index].span);
                return std::nullopt;
            }
            stream_operands.push_back(
                expression.operands[index]);
        }
        const Expression ordinary_stream {
            ExpressionKind::Concatenation,
            "concat",
            std::move(stream_operands),
            expression.span
        };
        const auto width = infer_width(ordinary_stream);
        if (!width || *width == 0 || *width > 64) {
            report(
                "FSIM-ELAB-SVEXPR-003",
                "streaming concatenation requires a statically known "
                "packed result width from 1 through 64",
                expression.span);
            return std::nullopt;
        }
        const auto source = lower_expression(
            ordinary_stream, *width);
        if (!source) {
            return std::nullopt;
        }
        if (expression.text == "@stream-right"
            || static_cast<std::size_t>(*slice_size) >= *width) {
            return *source;
        }
        std::vector<RegisterId> slices;
        for (std::size_t offset = 0; offset < *width;) {
            const auto chunk = std::min(
                static_cast<std::size_t>(*slice_size),
                *width - offset);
            const auto slice = allocate_register(
                chunk, register_domain(*source));
            process_.operations.emplace_back(Extract {
                slice,
                *source,
                static_cast<std::uint32_t>(offset),
                static_cast<std::uint32_t>(chunk) });
            slices.push_back(slice);
            offset += chunk;
        }
        const auto destination = allocate_register(
            *width, register_domain(*source));
        process_.operations.emplace_back(Concatenate {
            destination,
            std::move(slices),
            static_cast<std::uint32_t>(*width) });
        return destination;
    }
    if (language_ == frontend::Language::Vhdl2008
        && expression.kind == ExpressionKind::Call
        && expression.text == "endfile") {
        if (expression.operands.size() != 1
            || expression.operands.front().kind
                != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-VHFILE-009",
                "endfile requires exactly one whole VHDL file object",
                expression.span);
            return std::nullopt;
        }
        const auto& file = expression.operands.front();
        const auto local = locals_.find(file.text);
        const auto* type = object_type(file.text);
        if (local == locals_.end() || type == nullptr
            || !type->vhdl_file) {
            report(
                "FSIM-ELAB-VHFILE-009",
                "unknown VHDL file object '" + file.text + "'",
                file.span);
            return std::nullopt;
        }
        const auto destination = allocate_register(
            1, frontend::ValueDomain::Boolean);
        process_.operations.emplace_back(FileEndOfFile {
            destination, local->second, true });
        return destination;
    }
    const auto lower_handle =
        [&](const Expression& handle)
        -> std::optional<RegisterId> {
        const auto* type = handle.kind == ExpressionKind::Identifier
            ? object_type(handle.text)
            : nullptr;
        const bool integer_handle = handle.kind == ExpressionKind::IntegerLiteral
            || (type != nullptr
                && type->domain
                    == frontend::ValueDomain::Integer)
            || (handle.kind == ExpressionKind::Call
                && (handle.text == "$fopen"
                    || handle.text == "$fgets"
                    || handle.text == "$fgetc"
                    || handle.text == "$ungetc"
                    || handle.text == "$feof"
                    || handle.text == "$ferror"
                    || handle.text == "$fscanf"
                    || handle.text == "$sscanf"
                    || handle.text == "$fread"
                    || handle.text == "$fseek"
                    || handle.text == "$ftell"
                    || handle.text == "$rewind"))
            || is_integer_expression(handle);
        if (!integer_handle) {
            report(
                "FSIM-ELAB-SVFILE-001",
                "a file handle must be a 32-bit integer expression",
                handle.span);
            return std::nullopt;
        }
        auto value = lower_expression(handle, 32);
        if (value && register_width(*value) != 32) {
            *value = resize_register(
                *value, 32, is_signed_expression(handle));
        }
        return value;
    };
    struct FileTextTarget {
        FileTextTargetKind kind { FileTextTargetKind::string_register };
        std::uint32_t id { };
        std::uint32_t width { 1 };
        std::optional<StringObjectId> string_object;
    };
    const auto text_target =
        [&](const Expression& target) -> std::optional<FileTextTarget> {
        if (target.kind != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-SVFILE-002",
                "a file read/error target must be a whole writable packed or "
                "string variable",
                target.span);
            return std::nullopt;
        }
        if (const auto local = string_locals_.find(target.text);
            local != string_locals_.end()) {
            return FileTextTarget {
                FileTextTargetKind::string_register, local->second, 1, { }
            };
        }
        if (const auto object = string_objects_.find(target.text);
            object != string_objects_.end()) {
            if (read_only_string_objects_.contains(object->second)) {
                report(
                    "FSIM-ELAB-SVPORT-011",
                    "an input mutable string port is read-only",
                    target.span);
                return std::nullopt;
            }
            return FileTextTarget {
                FileTextTargetKind::string_register,
                allocate_string_register(), 1, object->second
            };
        }
        if (const auto local = locals_.find(target.text);
            local != locals_.end()) {
            const auto width = register_width(local->second);
            if (width != 0
                && width <= std::numeric_limits<std::uint32_t>::max()) {
                return FileTextTarget {
                    FileTextTargetKind::packed_register, local->second,
                    static_cast<std::uint32_t>(width), { }
                };
            }
        }
        if (const auto signal = signals_.find(target.text);
            signal != signals_.end()
            && !read_only_signals_.contains(signal->second)) {
            const auto width = design_.signal_info_[signal->second].width;
            if (width != 0
                && width <= std::numeric_limits<std::uint32_t>::max()) {
                return FileTextTarget {
                    FileTextTargetKind::packed_signal, signal->second,
                    static_cast<std::uint32_t>(width), { }
                };
            }
        }
        report(
            "FSIM-ELAB-SVFILE-002",
            "unknown, read-only, or unsupported file text target '"
                + target.text + "'",
            target.span);
        return std::nullopt;
    };
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$fopen") {
        if (language_ == frontend::Language::Vhdl2008
            || expression.operands.empty()
            || expression.operands.size() > 2
            || !is_string_expression(expression.operands[0])
            || (expression.operands.size() == 2
                && !is_string_expression(
                    expression.operands[1]))) {
            report(
                "FSIM-ELAB-SVFILE-003",
                "$fopen requires a Verilog byte-string filename "
                "and optional mode expression",
                expression.span);
            return std::nullopt;
        }
        const auto path = lower_string_expression(expression.operands[0]);
        std::optional<StringRegisterId> mode;
        if (expression.operands.size() == 2) {
            mode = lower_string_expression(expression.operands[1]);
        } else {
            mode = allocate_string_register();
            process_.operations.emplace_back(
                LoadStringConstant {
                    *mode, "\x1f"
                           "fsim-multichannel-write" });
        }
        if (!path || !mode) {
            return std::nullopt;
        }
        const auto destination = allocate_register(32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            FileOpen { destination, *path, *mode });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$fgets") {
        if (language_ == frontend::Language::Vhdl2008
            || expression.operands.size() != 2) {
            report(
                "FSIM-ELAB-SVFILE-004",
                "$fgets requires a writable packed or string target and integer handle",
                expression.span);
            return std::nullopt;
        }
        const auto target = text_target(expression.operands[0]);
        const auto handle = lower_handle(expression.operands[1]);
        if (!target || !handle) {
            return std::nullopt;
        }
        const auto destination = allocate_register(32, frontend::ValueDomain::Bit2);
        FileReadLine operation {
            destination, *handle, target->id, 0, FileReadKind::line
        };
        operation.target_kind = target->kind;
        operation.target_width = target->width;
        process_.operations.emplace_back(operation);
        if (target->string_object) {
            process_.operations.emplace_back(
                WriteStringObject { *target->string_object, target->id });
        }
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$fgetc") {
        if (language_ == frontend::Language::Vhdl2008
            || expression.operands.size() != 1) {
            report(
                "FSIM-ELAB-SVFILE-009",
                "$fgetc requires one integer handle",
                expression.span);
            return std::nullopt;
        }
        const auto handle = lower_handle(expression.operands[0]);
        if (!handle) {
            return std::nullopt;
        }
        const auto destination = allocate_register(32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            FileReadLine {
                destination, *handle, 0, 0,
                FileReadKind::character });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$ungetc") {
        if (language_ == frontend::Language::Vhdl2008
            || expression.operands.size() != 2) {
            report(
                "FSIM-ELAB-SVFILE-010",
                "$ungetc requires a character and integer handle",
                expression.span);
            return std::nullopt;
        }
        auto character = lower_expression(expression.operands[0], 32);
        const auto handle = lower_handle(expression.operands[1]);
        if (!character || !handle) {
            return std::nullopt;
        }
        if (register_width(*character) != 32) {
            *character = resize_register(
                *character, 32,
                is_signed_expression(expression.operands[0]));
        }
        const auto destination = allocate_register(32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            FileReadLine {
                destination, *handle, 0, *character,
                FileReadKind::unget });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$feof") {
        if (language_ == frontend::Language::Vhdl2008
            || expression.operands.size() != 1) {
            report(
                "FSIM-ELAB-SVFILE-005",
                "$feof requires one integer handle",
                expression.span);
            return std::nullopt;
        }
        const auto handle = lower_handle(expression.operands[0]);
        if (!handle) {
            return std::nullopt;
        }
        const auto destination = allocate_register(32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            FileEndOfFile { destination, *handle });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$ferror") {
        if (language_ == frontend::Language::Vhdl2008
            || expression.operands.size() != 2) {
            report(
                "FSIM-ELAB-SVFILE-006",
                "$ferror requires an integer handle and writable packed or string target",
                expression.span);
            return std::nullopt;
        }
        const auto handle = lower_handle(expression.operands[0]);
        const auto target = text_target(expression.operands[1]);
        if (!handle || !target) {
            return std::nullopt;
        }
        const auto destination = allocate_register(32, frontend::ValueDomain::Bit2);
        FileErrorStatus operation { destination, *handle, target->id };
        operation.target_kind = target->kind;
        operation.target_width = target->width;
        process_.operations.emplace_back(operation);
        if (target->string_object) {
            process_.operations.emplace_back(
                WriteStringObject { *target->string_object, target->id });
        }
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$fseek"
            || expression.text == "$ftell"
            || expression.text == "$rewind")) {
        const bool seek = expression.text == "$fseek";
        const bool named = std::ranges::any_of(
            expression.call_argument_names,
            [](const std::string& name) { return !name.empty(); });
        if (language_ == frontend::Language::Vhdl2008
            || named
            || expression.operands.size() != (seek ? 3U : 1U)) {
            report(
                "FSIM-ELAB-SVFILE-015",
                expression.text
                    + " requires an integer handle"
                    + (seek ? ", offset, and origin" : ""),
                expression.span);
            return std::nullopt;
        }
        const auto handle = lower_handle(expression.operands[0]);
        if (!handle)
            return std::nullopt;
        FilePosition operation;
        operation.handle = *handle;
        operation.kind = expression.text == "$fseek"
            ? FilePositionKind::seek
            : expression.text == "$ftell"
            ? FilePositionKind::tell
            : FilePositionKind::rewind;
        const auto lower_integer = [&](const std::size_t operand)
            -> std::optional<RegisterId> {
            const auto& value_expression = expression.operands[operand];
            const auto width = infer_width(value_expression);
            if (!width || *width == 0
                || is_string_expression(value_expression)
                || is_container_expression(value_expression)) {
                report(
                    "FSIM-ELAB-SVFILE-015",
                    "$fseek offset and origin must be integer expressions",
                    value_expression.span);
                return std::nullopt;
            }
            auto value = lower_expression(value_expression, 32);
            if (value && register_width(*value) != 32) {
                *value = resize_register(
                    *value, 32, is_signed_expression(value_expression));
            }
            return value;
        };
        if (seek) {
            const auto offset = lower_integer(1);
            const auto origin = lower_integer(2);
            if (!offset || !origin)
                return std::nullopt;
            operation.offset = *offset;
            operation.origin = *origin;
        }
        operation.destination = allocate_register(32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(operation);
        return operation.destination;
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$dimensions"
            || expression.text == "$unpacked_dimensions")) {
        if (language_
                != frontend::Language::SystemVerilog2017
            || expression.operands.size() != 1) {
            report(
                "FSIM-ELAB-090",
                expression.text
                    + " requires SystemVerilog and exactly one "
                      "statically sized packed argument",
                expression.span);
            return std::nullopt;
        }
        const auto operand_width = infer_width(expression.operands.front());
        const auto range = operand_width
            ? expression_range(
                  expression.operands.front(),
                  *operand_width)
            : std::nullopt;
        if (!operand_width || !range || *operand_width == 0) {
            report(
                "FSIM-ELAB-090",
                expression.text
                    + " cannot infer a static packed dimension "
                      "for its argument",
                expression.operands.front().span);
            return std::nullopt;
        }
        const auto destination = allocate_register(
            32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            destination,
            unsigned_value(
                expression.text == "$dimensions" ? 1 : 0,
                32) });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$onehot"
            || expression.text == "$onehot0")) {
        if (language_
                != frontend::Language::SystemVerilog2017
            || expression.operands.size() != 1) {
            report(
                "FSIM-ELAB-087",
                expression.text
                    + " requires SystemVerilog and exactly one "
                      "packed argument",
                expression.span);
            return std::nullopt;
        }
        const auto source_width = infer_width(expression.operands.front())
                                      .value_or(expected_width);
        const auto source = lower_expression(
            expression.operands.front(), source_width);
        if (!source) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            1, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Reduction {
            expression.text == "$onehot"
                ? ReductionOperator::one_hot
                : ReductionOperator::one_hot_or_zero,
            destination,
            *source });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$countones") {
        if (language_
                != frontend::Language::SystemVerilog2017
            || expression.operands.size() != 1) {
            report(
                "FSIM-ELAB-088",
                "$countones requires SystemVerilog and exactly one "
                "packed argument",
                expression.span);
            return std::nullopt;
        }
        const auto source_width = infer_width(expression.operands.front());
        if (!source_width || *source_width == 0
            || *source_width
                > std::numeric_limits<std::uint32_t>::max()) {
            report(
                "FSIM-ELAB-088",
                "$countones cannot infer a representable static "
                "packed width for its argument",
                expression.operands.front().span);
            return std::nullopt;
        }
        const auto source = lower_expression(
            expression.operands.front(), *source_width);
        if (!source) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            CountOnes { destination, *source });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$countbits") {
        if (language_
                != frontend::Language::SystemVerilog2017
            || expression.operands.size() < 2) {
            report(
                "FSIM-ELAB-089",
                "$countbits requires SystemVerilog, one packed "
                "expression, and at least one constant one-bit "
                "control",
                expression.span);
            return std::nullopt;
        }
        const auto source_width = infer_width(expression.operands.front());
        if (!source_width || *source_width == 0
            || *source_width
                > std::numeric_limits<std::uint32_t>::max()) {
            report(
                "FSIM-ELAB-089",
                "$countbits cannot infer a representable static "
                "packed width for its expression",
                expression.operands.front().span);
            return std::nullopt;
        }
        std::uint8_t state_mask = 0;
        for (std::size_t index = 1;
            index < expression.operands.size();
            ++index) {
            const auto control = literal_value(
                expression.operands[index],
                1,
                frontend::Language::SystemVerilog2017);
            if (!control || control->value.width() != 1) {
                report(
                    "FSIM-ELAB-089",
                    "$countbits controls must be constant one-bit "
                    "0, 1, X, or Z values",
                    expression.operands[index].span);
                return std::nullopt;
            }
            const auto state = static_cast<std::uint8_t>(
                control->value.get(0));
            state_mask |= static_cast<std::uint8_t>(
                std::uint8_t { 1 } << state);
        }
        const auto source = lower_expression(
            expression.operands.front(), *source_width);
        if (!source) {
            return std::nullopt;
        }
        const auto destination = allocate_register(
            32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            CountBits {
                destination, *source, state_mask });
        return destination;
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "rising_edge"
            || expression.text == "falling_edge")) {
        report(
            "FSIM-ELAB-045",
            "a VHDL edge predicate is executable only as the sole, "
            "else-free outer statement of a sensitive process",
            expression.span);
        return std::nullopt;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "?:"
        && expression.operands.size() == 3) {
        if (language_ != frontend::Language::Vhdl2008) {
            const auto condition = lower_condition(
                expression.operands[0],
                "FSIM-ELAB-064",
                "conditional-expression");
            if (!condition) {
                return std::nullopt;
            }
            const auto true_width = infer_width(expression.operands[1])
                                        .value_or(expected_width);
            const auto false_width = infer_width(expression.operands[2])
                                         .value_or(expected_width);
            const auto value_width = std::max(
                expected_width,
                std::max(true_width, false_width));
            const bool result_signed = is_signed_expression(expression.operands[1])
                && is_signed_expression(expression.operands[2]);

            const auto known_one = allocate_register(
                1, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                known_one, unsigned_value(1, 1) });
            const auto definitely_true = allocate_register(
                1, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Binary {
                BinaryOperator::case_equal,
                definitely_true,
                *condition,
                known_one });
            const auto true_branch = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                definitely_true,
                0,
                true_branch + 1,
                UnknownBranchPolicy::when_false });

            const auto known_zero = allocate_register(
                1, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                known_zero, unsigned_value(0, 1) });
            const auto definitely_false = allocate_register(
                1, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Binary {
                BinaryOperator::case_equal,
                definitely_false,
                *condition,
                known_zero });
            const auto false_branch = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                definitely_false,
                0,
                false_branch + 1,
                UnknownBranchPolicy::when_false });

            auto unknown_true = lower_expression(
                expression.operands[1],
                value_width,
                expected_type);
            auto unknown_false = lower_expression(
                expression.operands[2],
                value_width,
                expected_type);
            if (!unknown_true || !unknown_false) {
                return std::nullopt;
            }
            *unknown_true = resize_register(
                *unknown_true, value_width, result_signed);
            *unknown_false = resize_register(
                *unknown_false, value_width, result_signed);
            const auto result_domain = register_domain(*condition)
                        == frontend::ValueDomain::Bit2
                    && is_two_state_domain(
                        register_domain(*unknown_true))
                    && is_two_state_domain(
                        register_domain(*unknown_false))
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Logic4;
            const auto destination = allocate_register(
                value_width, result_domain);
            process_.operations.emplace_back(ConditionalSelect {
                destination,
                *condition,
                *unknown_true,
                *unknown_false });
            const auto unknown_exit = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Jump { 0 });

            const auto true_start = static_cast<InstructionIndex>(
                process_.operations.size());
            auto when_true = lower_expression(
                expression.operands[1],
                value_width,
                expected_type);
            if (!when_true) {
                return std::nullopt;
            }
            *when_true = resize_register(
                *when_true, value_width, result_signed);
            process_.operations.emplace_back(
                CopyRegister { destination, *when_true });
            const auto true_exit = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Jump { 0 });

            const auto false_start = static_cast<InstructionIndex>(
                process_.operations.size());
            auto when_false = lower_expression(
                expression.operands[2],
                value_width,
                expected_type);
            if (!when_false) {
                return std::nullopt;
            }
            *when_false = resize_register(
                *when_false, value_width, result_signed);
            process_.operations.emplace_back(
                CopyRegister { destination, *when_false });
            const auto end = static_cast<InstructionIndex>(
                process_.operations.size());

            process_.operations[true_branch] = Branch {
                definitely_true,
                true_start,
                true_branch + 1,
                UnknownBranchPolicy::when_false
            };
            process_.operations[false_branch] = Branch {
                definitely_false,
                false_start,
                false_branch + 1,
                UnknownBranchPolicy::when_false
            };
            process_.operations[unknown_exit] = Jump { end };
            process_.operations[true_exit] = Jump { end };
            return destination;
        }
        const auto condition = lower_expression(expression.operands[0], 1);
        if (!condition) {
            return std::nullopt;
        }
        if (register_width(*condition) != 1) {
            report(
                "FSIM-ELAB-064",
                "a conditional-expression condition must produce one "
                "bit in this executable slice",
                expression.operands[0].span);
            return std::nullopt;
        }
        if (language_ == frontend::Language::Vhdl2008
            && register_domain(*condition)
                != frontend::ValueDomain::Boolean) {
            report(
                "FSIM-ELAB-092",
                "a VHDL conditional-assignment condition must have "
                "type boolean",
                expression.operands[0].span);
            return std::nullopt;
        }
        const auto value_width = infer_width(expression.operands[1])
                                     .value_or(
                                         infer_width(expression.operands[2])
                                             .value_or(expected_width));
        auto result_domain = frontend::ValueDomain::Logic4;
        if (is_integer_expression(expression.operands[1])
            && is_integer_expression(expression.operands[2])) {
            result_domain = frontend::ValueDomain::Integer;
        } else if (expected_type != nullptr
            && expected_type->domain
                != frontend::ValueDomain::Unknown) {
            result_domain = expected_type->domain;
        } else if (const auto type = vhdl_expression_type(expression)) {
            result_domain = type->domain;
        }
        const auto destination = allocate_register(value_width, result_domain);
        const auto branch_index = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Branch {
            *condition, 0, 0, UnknownBranchPolicy::error });

        const auto true_start = static_cast<InstructionIndex>(
            process_.operations.size());
        const auto when_true = lower_expression(
            expression.operands[1], value_width, expected_type);
        if (!when_true) {
            return std::nullopt;
        }
        process_.operations.emplace_back(
            CopyRegister { destination, *when_true });
        const auto true_exit = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Jump { 0 });

        const auto false_start = static_cast<InstructionIndex>(
            process_.operations.size());
        const auto when_false = lower_expression(
            expression.operands[2], value_width, expected_type);
        if (!when_false) {
            return std::nullopt;
        }
        if (register_width(*when_true)
            != register_width(*when_false)) {
            report(
                "FSIM-ELAB-065",
                "conditional-expression alternatives have different "
                "widths ("
                    + std::to_string(register_width(*when_true))
                    + " and "
                    + std::to_string(register_width(*when_false))
                    + ")",
                expression.span);
            return std::nullopt;
        }
        process_.operations.emplace_back(
            CopyRegister { destination, *when_false });
        const auto end = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations[branch_index] = Branch {
            *condition,
            true_start,
            false_start,
            UnknownBranchPolicy::error
        };
        process_.operations[true_exit] = Jump { end };
        return destination;
    }

    return ExpressionAttempt { };
}

} // namespace fsim::elaboration
