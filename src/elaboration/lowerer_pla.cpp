// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

bool Lowerer::lower_pla_task(const Statement& statement)
{
    bool asynchronous { };
    auto remainder = std::string_view { statement.task_name };
    if (remainder.starts_with("$async$")) {
        asynchronous = true;
        remainder.remove_prefix(7U);
    } else if (remainder.starts_with("$sync$")) {
        remainder.remove_prefix(6U);
    } else {
        return false;
    }

    PlaLogicKind logic;
    if (remainder.starts_with("and$")) {
        logic = PlaLogicKind::and_logic;
        remainder.remove_prefix(4U);
    } else if (remainder.starts_with("nand$")) {
        logic = PlaLogicKind::nand_logic;
        remainder.remove_prefix(5U);
    } else if (remainder.starts_with("or$")) {
        logic = PlaLogicKind::or_logic;
        remainder.remove_prefix(3U);
    } else if (remainder.starts_with("nor$")) {
        logic = PlaLogicKind::nor_logic;
        remainder.remove_prefix(4U);
    } else {
        return false;
    }
    const bool plane = remainder == "plane";
    if (!plane && remainder != "array") {
        return false;
    }

    const bool named = std::ranges::any_of(
        statement.task_argument_names,
        [](const std::string& name) { return !name.empty(); });
    if (language_ != frontend::Language::SystemVerilog2017
        || named || statement.task_arguments.size() != 3U
        || statement.task_arguments[0].kind
            != frontend::ExpressionKind::Identifier) {
        report(
            "FSIM-ELAB-SVPLA-001",
            statement.task_name
                + " requires a fixed personality memory, packed input terms, and a packed variable output",
            statement.span);
        return true;
    }

    const auto memory_name = statement.task_arguments[0].text;
    const auto memory = container_objects_.find(memory_name);
    if (memory == container_objects_.end()
        || static_cast<std::size_t>(memory->second)
            >= design_.container_objects().size()) {
        report(
            "FSIM-ELAB-SVPLA-001",
            "PLA personality requires a direct fixed-array object",
            statement.task_arguments[0].span);
        return true;
    }
    const auto& memory_info = design_.container_objects().at(memory->second);
    const auto& memory_type = memory_info.type;
    const auto packed_memory
        = memory_type.element_kind == ContainerElementKind::Packed
        || memory_type.element_kind == ContainerElementKind::Scalar;
    if (!memory_type.fixed || !packed_memory
        || memory_type.dimensions.size() != 1U
        || memory_type.dimensions.front().first
            > memory_type.dimensions.front().second
        || memory_type.element_width == 0U
        || memory_info.slice_alias) {
        report(
            "FSIM-ELAB-SVPLA-001",
            "PLA personality must be an ascending one-dimensional fixed packed array",
            statement.task_arguments[0].span);
        return true;
    }
    const auto row_count = static_cast<std::uint64_t>(
                               static_cast<std::int64_t>(
                                   memory_type.dimensions.front().second)
                               - memory_type.dimensions.front().first)
        + 1U;
    if (row_count == 0U
        || row_count > std::numeric_limits<std::uint32_t>::max()) {
        report(
            "FSIM-ELAB-SVPLA-001",
            "PLA output width is outside the runtime packed-width representation",
            statement.task_arguments[0].span);
        return true;
    }
    const auto input_width = memory_type.element_width;
    const auto output_width = static_cast<std::uint32_t>(row_count);
    if (infer_width(statement.task_arguments[1])
            != std::optional<std::uint64_t> { input_width }
        || infer_width(statement.task_arguments[2])
            != std::optional<std::uint64_t> { output_width }) {
        report(
            "FSIM-ELAB-SVPLA-001",
            "PLA input and output widths must match the personality columns and rows",
            statement.span);
        return true;
    }

    auto output_target = capture_callable_copy_out_target(
        statement.task_arguments[2],
        "@pla_output_target_"
            + std::to_string(process_.operations.size()));
    if (!output_target) {
        return true;
    }
    frontend::Type output_type;
    output_type.spelling = "logic";
    output_type.domain = frontend::ValueDomain::Logic4;
    output_type.packed_range = frontend::PackedRange {
        static_cast<std::int64_t>(output_width) - 1, 0, true
    };

    std::set<std::string> identifiers;
    collect_identifiers(statement.task_arguments[1], identifiers);
    std::vector<SignalId> input_signals;
    for (const auto& identifier : identifiers) {
        if (const auto signal = signals_.find(identifier);
            signal != signals_.end()) {
            input_signals.push_back(signal->second);
        }
    }
    std::ranges::sort(input_signals);
    input_signals.erase(
        std::ranges::unique(input_signals).begin(), input_signals.end());

    const auto emit_evaluation = [&]() -> bool {
        auto input = lower_expression(
            statement.task_arguments[1], input_width);
        if (!input) {
            return false;
        }
        if (register_width(*input) != input_width) {
            *input = resize_register(
                *input, input_width,
                is_signed_expression(statement.task_arguments[1]));
        }
        const auto output = allocate_register(
            output_width, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(PlaEvaluate {
            memory->second,
            *input,
            output,
            input_width,
            output_width,
            logic,
            plane });
        lower_callable_copy_out(
            *output_target,
            output_type,
            output,
            { },
            { },
            false,
            false,
            "@pla_output_"
                + std::to_string(process_.operations.size()));
        return true;
    };

    if (!asynchronous) {
        (void)emit_evaluation();
        return true;
    }
    if (active_function_ || active_task_ || active_procedure_) {
        report(
            "FSIM-ELAB-SVPLA-001",
            "an asynchronous PLA cannot escape a callable frame",
            statement.span);
        return true;
    }

    const auto fork_instruction = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Fork { });
    const auto continuation_jump = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Jump { });
    const auto branch = static_cast<InstructionIndex>(
        process_.operations.size());
    const auto loop = branch;
    if (!emit_evaluation()) {
        return true;
    }
    process_.operations.emplace_back(WaitPla {
        memory->second, std::move(input_signals) });
    process_.operations.emplace_back(Jump { loop });
    const auto continuation = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations[continuation_jump] = Jump { continuation };
    process_.operations[fork_instruction] = Fork {
        { branch }, ForkJoinKind::none
    };
    return true;
}

} // namespace fsim::elaboration
