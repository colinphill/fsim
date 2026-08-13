// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"
#include "lowerer_driver_regions.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

Process Lowerer::lower_concurrent(
    const Statement& statement,
    const frontend::Language language,
    const std::string& name,
    const std::size_t order)
{
    process_ = Process { };
    generated_processes_.clear();
    implicit_signal_dependencies_.clear();
    language_ = language;
    hierarchy_ = name;
    process_kind_ = ProcessKind::VhdlProcess;
    next_register_ = 0;
    next_string_register_ = 0;
    next_container_register_ = 0;
    register_widths_.clear();
    register_domains_.clear();
    locals_.clear();
    string_locals_.clear();
    container_locals_.clear();
    local_signed_.clear();
    local_ranges_.clear();
    local_integer_ranges_.clear();
    local_members_.clear();
    local_types_.clear();
    declaration_registers_.clear();
    debug_local_names_.clear();
    local_scope_.clear();
    loop_controls_.clear();
    process_.id = static_cast<ProcessId>(design_.processes_.size());
    process_.program_owner = systemverilog_program_owner_;
    process_.postponed = statement.vhdl_postponed;
    process_.name = name + "."
        + (statement.label.empty()
                ? "concurrent_" + std::to_string(order)
                : statement.label);
    if (statement.verilog_drive_strength) {
        const auto rank = [](const frontend::VerilogStrength strength) {
            using Frontend = frontend::VerilogStrength;
            switch (strength) {
            case Frontend::HighZ:
                return StrengthRank::highz;
            case Frontend::Small:
                return StrengthRank::small;
            case Frontend::Medium:
                return StrengthRank::medium;
            case Frontend::Weak:
                return StrengthRank::weak;
            case Frontend::Large:
                return StrengthRank::large;
            case Frontend::Pull:
                return StrengthRank::pull;
            case Frontend::Strong:
                return StrengthRank::strong;
            case Frontend::Supply:
                return StrengthRank::supply;
            }
            return StrengthRank::strong;
        };
        process_.drive_strength = DriveStrength {
            rank(statement.verilog_drive_strength->zero),
            rank(statement.verilog_drive_strength->one)
        };
    }
    if (statement.verilog_switch_driver) {
        const auto endpoint = [&](const frontend::Expression& expression)
            -> std::optional<SignalId> {
            const auto* base = &expression;
            while ((base->kind == frontend::ExpressionKind::Index
                       || base->kind == frontend::ExpressionKind::Slice)
                && !base->operands.empty()) {
                base = &base->operands.front();
            }
            if (base->kind != frontend::ExpressionKind::Identifier) {
                return std::nullopt;
            }
            const auto found = signals_.find(base->text);
            return found == signals_.end()
                ? std::nullopt
                : std::optional<SignalId> { found->second };
        };
        process_.switch_source = endpoint(statement.verilog_switch_source);
        if (statement.verilog_switch_control.valid()) {
            process_.switch_control = endpoint(statement.verilog_switch_control);
            process_.switch_active_high = statement.verilog_switch_active_high;
        }
        process_.switch_bidirectional = statement.verilog_switch_bidirectional;
        process_.switch_resistive = statement.verilog_switch_resistive;
    }
    class_tasks_.clear();
    collect_class_tasks(std::vector<Statement> { statement });
    initialize_function_support();
    initialize_task_support();
    initialize_procedure_support();
    emit_debug_point(DebugPointKind::process_entry, statement.span);

    std::set<std::string> dependencies;
    collect_wildcard_identifiers(
        std::vector<Statement> { statement }, dependencies);
    for (const auto& dependency : dependencies) {
        if (const auto found = signals_.find(dependency);
            found != signals_.end()
            && design_.signal_info_[found->second].width != 0U) {
            process_.static_sensitivity.push_back(
                { found->second, runtime::simir::EdgeKind::any });
        }
    }
    if (!statement.vhdl_guarded_assignment) {
        lower_statement(statement);
    } else if (!statement.vhdl_guard.valid()) {
        report(
            "FSIM-ELAB-VHDLGUARD-001",
            "a guarded concurrent assignment requires an enclosing "
            "Boolean-guarded block",
            statement.span);
    } else {
        const auto guard = lower_condition(
            statement.vhdl_guard,
            "FSIM-ELAB-VHDLGUARD-001",
            "guarded concurrent assignment");
        if (guard) {
            const auto branch = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                *guard, 0, 0, UnknownBranchPolicy::error });
            const auto active_start = static_cast<InstructionIndex>(
                process_.operations.size());
            auto active = statement;
            active.vhdl_guarded_assignment = false;
            lower_statement(active);
            const auto exit = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Jump { 0 });
            const auto inactive_start = static_cast<InstructionIndex>(
                process_.operations.size());
            const auto find_assignment = [&](const auto& self,
                                             const Statement& node)
                -> const Statement* {
                if (node.kind == StatementKind::Assignment)
                    return &node;
                for (const auto& child : node.statements) {
                    if (const auto* found = self(self, child))
                        return found;
                }
                for (const auto& child : node.else_statements) {
                    if (const auto* found = self(self, child))
                        return found;
                }
                for (const auto& alternative : node.case_alternatives) {
                    for (const auto& child : alternative.statements) {
                        if (const auto* found = self(self, child))
                            return found;
                    }
                }
                return nullptr;
            };
            if (const auto* source = find_assignment(find_assignment, statement)) {
                auto disconnect = *source;
                disconnect.vhdl_guarded_assignment = false;
                disconnect.vhdl_unaffected = false;
                disconnect.value = { };
                disconnect.delay = statement.vhdl_disconnection_delay;
                disconnect.vhdl_waveform.clear();
                disconnect.vhdl_waveform.push_back(
                    frontend::VhdlWaveformElement {
                        { },
                        statement.vhdl_disconnection_delay,
                        true,
                        source->span });
                lower_assignment(disconnect);
            } else {
                report(
                    "FSIM-ELAB-VHDLGUARD-001",
                    "guarded concurrent-assignment HIR has no assignment leaf",
                    statement.span);
            }
            const auto end = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations[branch] = Branch {
                *guard,
                active_start,
                inactive_start,
                UnknownBranchPolicy::error
            };
            process_.operations[exit] = Jump { end };
        }
    }
    for (const auto dependency : implicit_signal_dependencies_) {
        if (design_.signal_info_[dependency].width != 0U) {
            process_.static_sensitivity.push_back(
                { dependency, runtime::simir::EdgeKind::any });
        }
    }
    if (!process_.static_sensitivity.empty()) {
        process_.operations.emplace_back(WaitSensitivity { });
        process_.operations.emplace_back(Jump { 0 });
    } else {
        process_.operations.emplace_back(Halt { });
    }
    lower_pending_tasks();
    lower_pending_procedures();
    lower_pending_functions();
    validate_read_only_signal_writes(statement.span);
    process_.register_count = next_register_;
    process_.string_register_count = next_string_register_;
    process_.container_register_count = next_container_register_;
    process_.register_value_kinds.reserve(register_domains_.size());
    for (const auto domain : register_domains_) {
        process_.register_value_kinds.push_back(value_kind(domain));
    }
    process_.driver_regions = collect_driver_regions(
        process_, register_widths_);
    if (process_.switch_source && process_.driver_regions.size() == 1) {
        const auto& target = process_.driver_regions.front();
        process_.switch_target = target.signal;
        const auto source_width = design_.signals_.at(*process_.switch_source)
                                      .initial_value.width();
        std::uint64_t source_offset { };
        std::uint64_t selected_source_width = source_width;
        bool source_selected = false;
        const auto& source = statement.verilog_switch_source;
        if (source.kind == frontend::ExpressionKind::Index
            && source.operands.size() == 2) {
            const auto index = static_integer_value(source.operands[1]);
            const auto offset = index
                ? select_offset(source.operands[0], *index, source_width)
                : std::optional<std::size_t> { };
            if (offset) {
                source_offset = *offset;
                selected_source_width = 1;
                source_selected = true;
            }
        } else if (source.kind == frontend::ExpressionKind::Slice) {
            const auto selection = constant_slice_selection(source, source_width);
            if (selection) {
                source_offset = selection->offset;
                selected_source_width = selection->width;
                source_selected = true;
            }
        }
        const auto target_width = target.whole
            ? design_.signals_.at(target.signal).initial_value.width()
            : static_cast<std::size_t>(target.width);
        if ((source_selected || !target.whole)
            && selected_source_width == target_width) {
            process_.switch_source_offset = source_offset;
            process_.switch_target_offset = target.whole ? 0 : target.offset;
            process_.switch_width = selected_source_width;
        }
    }
    validate_vhdl_driver_attributes(statement.span);
    next_register_ = 0;
    next_string_register_ = 0;
    next_container_register_ = 0;
    register_widths_.clear();
    register_domains_.clear();
    locals_.clear();
    string_locals_.clear();
    container_locals_.clear();
    local_signed_.clear();
    local_ranges_.clear();
    local_integer_ranges_.clear();
    local_members_.clear();
    local_types_.clear();
    declaration_registers_.clear();
    debug_local_names_.clear();
    local_scope_.clear();
    loop_controls_.clear();
    return std::move(process_);
}

} // namespace fsim::elaboration
