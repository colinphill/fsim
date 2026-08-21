// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"
#include "lowerer_driver_regions.hpp"

#include <cstdlib>
#include <iostream>
#include <map>
#include <numeric>

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

Process Lowerer::lower_concurrent_group(
    const std::span<const Statement> statements,
    const frontend::Language language,
    const std::string& name,
    const std::size_t order)
{
    if (statements.empty()) {
        throw std::invalid_argument {
            "a fused concurrent group cannot be empty"
        };
    }
    process_ = Process { };
    generated_processes_.clear();
    implicit_signal_dependencies_.clear();
    language_ = language;
    hierarchy_ = name;
    process_kind_ = language == frontend::Language::Vhdl2008
        ? ProcessKind::VhdlProcess
        : ProcessKind::SystemVerilogAlwaysComb;
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
    block_controls_.clear();
    named_block_controls_.clear();
    named_fork_controls_.clear();
    procedural_continuous_assignments_.clear();
    procedural_continuous_assignment_by_statement_.clear();
    procedural_continuous_assignments_by_target_.clear();
    process_.id = static_cast<ProcessId>(design_.processes_.size());
    process_.program_owner = systemverilog_program_owner_;
    process_.name = name
        + (language == frontend::Language::Vhdl2008
                ? ".concurrent_fused_"
                : ".continuous_fused_")
        + std::to_string(order);

    class_tasks_.clear();
    collect_class_tasks(statements);
    initialize_function_support();
    initialize_task_support();
    initialize_procedure_support();

    std::vector<bool> static_assignment(statements.size());
    std::map<std::vector<SignalId>, std::size_t>
        source_sensitivity_group_sizes;
    for (std::size_t index = 0; index < statements.size(); ++index) {
        auto dependency_probe = statements[index];
        dependency_probe.target = Expression {
            ExpressionKind::IntegerLiteral,
            "0",
            { },
            statements[index].target.span
        };
        if ((statements[index].target.kind == ExpressionKind::Index
                || statements[index].target.kind == ExpressionKind::Slice)
            && statements[index].target.operands.size() > 1U) {
            dependency_probe.task_arguments.insert(
                dependency_probe.task_arguments.end(),
                std::next(statements[index].target.operands.begin()),
                statements[index].target.operands.end());
        }
        std::set<std::string> dependencies;
        collect_wildcard_identifiers(
            std::span<const Statement> { &dependency_probe, 1U },
            dependencies);
        static_assignment[index] = std::ranges::none_of(
            dependencies,
            [&](const std::string& dependency) {
                return signals_.contains(dependency)
                    || string_objects_.contains(dependency)
                    || container_objects_.contains(dependency);
            });
        std::vector<SignalId> source_sensitivity;
        for (const auto& dependency : dependencies) {
            if (const auto found = signals_.find(dependency);
                found != signals_.end()
                && design_.signal_info_[found->second].width != 0U) {
                source_sensitivity.push_back(found->second);
            }
        }
        std::ranges::sort(source_sensitivity);
        source_sensitivity.erase(
            std::ranges::unique(source_sensitivity).begin(),
            source_sensitivity.end());
        ++source_sensitivity_group_sizes[source_sensitivity];
    }
    if (std::getenv("FSIM_PROFILE_FUSED_SENSITIVITY") != nullptr) {
        const auto largest = std::ranges::max_element(
            source_sensitivity_group_sizes,
            { },
            [](const auto& group) { return group.second; });
        std::cerr << "fsim-profile: fused-sensitivity name='" << process_.name
                  << "' statements=" << statements.size()
                  << " groups=" << source_sensitivity_group_sizes.size()
                  << " largest="
                  << (largest == source_sensitivity_group_sizes.end()
                          ? 0U
                          : largest->second)
                  << '\n';
    }
    if (const auto* forwarding_filter
        = std::getenv("FSIM_PROFILE_FUSED_FORWARDING");
        forwarding_filter != nullptr
        && (std::string_view { forwarding_filter } == "1"
            || process_.name.find(forwarding_filter) != std::string::npos)) {
        std::map<std::string, std::size_t> calls;
        const auto collect_calls = [&](const auto& self,
                                       const Expression& expression,
                                       bool& statement_call) -> void {
            if (expression.kind == ExpressionKind::Call
                && expression.text != "?:") {
                ++calls[expression.text];
                statement_call = true;
            }
            for (const auto& operand : expression.operands) {
                self(self, operand, statement_call);
            }
        };
        std::size_t call_statements { };
        for (const auto& statement : statements) {
            bool statement_call { };
            collect_calls(collect_calls, statement.value, statement_call);
            call_statements += statement_call ? 1U : 0U;
        }
        std::cerr << "fsim-profile: fused-forwarding name='" << process_.name
                  << "' statements=" << statements.size()
                  << " call_statements=" << call_statements << " calls=";
        bool first = true;
        for (const auto& [call, count] : calls) {
            if (!first) {
                std::cerr << ',';
            }
            first = false;
            std::cerr << call << ':' << count;
        }
        std::cerr << '\n';
    }

    constexpr std::size_t minimum_forwarded_group_size = 8U;
    std::vector<std::size_t> statement_order(statements.size());
    std::iota(statement_order.begin(), statement_order.end(), std::size_t { });
    bool forwarding_acyclic { };
    if (language != frontend::Language::Vhdl2008
        && statements.size() >= minimum_forwarded_group_size) {
        const auto static_expression_key = [](
                                               const auto& self,
                                               const Expression& expression)
            -> std::optional<std::string> {
            if (expression.kind == ExpressionKind::IntegerLiteral
                || expression.kind == ExpressionKind::LogicLiteral
                || expression.kind == ExpressionKind::BooleanLiteral) {
                return expression.text;
            }
            if ((expression.kind == ExpressionKind::Unary
                    || expression.kind == ExpressionKind::Binary)
                && !expression.operands.empty()) {
                auto result = expression.text + "(";
                for (std::size_t index = 0;
                     index < expression.operands.size(); ++index) {
                    const auto operand = self(self, expression.operands[index]);
                    if (!operand) {
                        return std::nullopt;
                    }
                    if (index != 0U) {
                        result += ",";
                    }
                    result += *operand;
                }
                result += ")";
                return result;
            }
            return std::nullopt;
        };
        const auto expression_key = [&static_expression_key](
                                        const auto& self,
                                        const Expression& expression)
            -> std::optional<std::string> {
            if (expression.kind == ExpressionKind::Identifier) {
                return expression.text;
            }
            if ((expression.kind == ExpressionKind::Index
                    || expression.kind == ExpressionKind::Slice)
                && !expression.operands.empty()) {
                auto base = self(self, expression.operands.front());
                if (!base) {
                    return std::nullopt;
                }
                auto result = *base + "[";
                for (std::size_t index = 1U;
                     index < expression.operands.size(); ++index) {
                    const auto& selector = expression.operands[index];
                    const auto selector_key = static_expression_key(
                        static_expression_key, selector);
                    if (!selector_key) {
                        return std::nullopt;
                    }
                    if (index != 1U) {
                        result += ":";
                    }
                    result += *selector_key;
                }
                result += "]";
                return result;
            }
            return std::nullopt;
        };
        const auto collect_reads = [&expression_key](
                                       const auto& self,
                                       const Expression& expression,
                                       std::vector<std::string>& reads) -> void {
            if (expression.kind == ExpressionKind::Identifier) {
                reads.push_back(expression.text);
                return;
            }
            if (expression.kind == ExpressionKind::Index
                || expression.kind == ExpressionKind::Slice) {
                if (const auto key = expression_key(
                        expression_key, expression)) {
                    reads.push_back(*key);
                    return;
                }
            }
            for (const auto& operand : expression.operands) {
                self(self, operand, reads);
            }
        };
        const auto overlaps = [](const std::string& left,
                                  const std::string& right) {
            if (left == right) {
                return true;
            }
            const auto selected_prefix = [](const std::string& prefix,
                                             const std::string& value) {
                return value.size() > prefix.size()
                    && value.starts_with(prefix)
                    && value[prefix.size()] == '[';
            };
            if (selected_prefix(left, right)
                || selected_prefix(right, left)) {
                return true;
            }
            const auto base = [](const std::string& key) {
                const auto selected = key.find('[');
                return key.substr(0, selected);
            };
            if (base(left) != base(right)) {
                return false;
            }
            return left.find('[') == std::string::npos
                || right.find('[') == std::string::npos;
        };

        std::vector<std::string> targets;
        std::vector<std::vector<std::string>> reads(statements.size());
        targets.reserve(statements.size());
        bool valid = true;
        const auto contains_call = [](const auto& self,
                                      const Expression& expression) -> bool {
            return (expression.kind == ExpressionKind::Call
                    && expression.text != "?:")
                || std::ranges::any_of(
                expression.operands,
                [&](const Expression& operand) {
                    return self(self, operand);
                });
        };
        for (std::size_t index = 0; index < statements.size(); ++index) {
            if (contains_call(contains_call, statements[index].value)) {
                valid = false;
                break;
            }
            const auto target = expression_key(
                expression_key, statements[index].target);
            if (!target) {
                valid = false;
                break;
            }
            targets.push_back(*target);
            collect_reads(
                collect_reads, statements[index].value, reads[index]);
            if (std::ranges::any_of(
                    reads[index],
                    [&](const std::string& read) {
                        return overlaps(*target, read);
                    })) {
                valid = false;
                break;
            }
        }
        std::vector<std::unordered_set<std::size_t>> successors(
            statements.size());
        std::vector<std::size_t> indegree(statements.size());
        if (valid) {
            for (std::size_t left = 0; left < statements.size(); ++left) {
                for (std::size_t right = left + 1U;
                     right < statements.size(); ++right) {
                    if (overlaps(targets[left], targets[right])) {
                        valid = false;
                        break;
                    }
                }
                if (!valid) {
                    break;
                }
            }
        }
        if (valid) {
            for (std::size_t writer = 0;
                 writer < statements.size(); ++writer) {
                for (std::size_t reader = 0;
                     reader < statements.size(); ++reader) {
                    if (writer == reader
                        || !std::ranges::any_of(
                            reads[reader],
                            [&](const std::string& read) {
                                return overlaps(targets[writer], read);
                            })) {
                        continue;
                    }
                    if (successors[writer].insert(reader).second) {
                        ++indegree[reader];
                    }
                }
            }
            statement_order.clear();
            statement_order.reserve(statements.size());
            std::vector<bool> emitted(statements.size());
            while (statement_order.size() < statements.size()) {
                std::optional<std::size_t> next;
                for (std::size_t candidate = 0;
                     candidate < statements.size(); ++candidate) {
                    if (!emitted[candidate]
                        && indegree[candidate] == 0U) {
                        next = candidate;
                        break;
                    }
                }
                if (!next) {
                    break;
                }
                emitted[*next] = true;
                statement_order.push_back(*next);
                for (const auto successor : successors[*next]) {
                    --indegree[successor];
                }
            }
            forwarding_acyclic
                = statement_order.size() == statements.size();
        }
        if (!forwarding_acyclic) {
            statement_order.resize(statements.size());
            std::iota(
                statement_order.begin(),
                statement_order.end(),
                std::size_t { });
        }
    }
    std::size_t hoisted_static_assignments { };
    if (!forwarding_acyclic) {
        const auto dynamic_begin = std::stable_partition(
            statement_order.begin(),
            statement_order.end(),
            [&](const std::size_t statement) {
                return static_assignment[statement];
            });
        hoisted_static_assignments = static_cast<std::size_t>(
            std::distance(statement_order.begin(), dynamic_begin));
        if (hoisted_static_assignments < minimum_forwarded_group_size) {
            hoisted_static_assignments = 0U;
            std::iota(
                statement_order.begin(),
                statement_order.end(),
                std::size_t { });
        }
    }
    if (std::getenv("FSIM_PROFILE_FUSED_STATIC") != nullptr
        && hoisted_static_assignments != 0U) {
        std::cerr << "fsim-profile: fused-static name=" << process_.name
                  << " count=" << hoisted_static_assignments << '\n';
        for (std::size_t hoisted = 0;
             hoisted < hoisted_static_assignments; ++hoisted) {
            const auto statement = statement_order[hoisted];
            std::cerr << "  statement=" << statement
                      << " target=" << statements[statement].target.text
                      << " value-kind="
                      << static_cast<int>(statements[statement].value.kind)
                      << " value=" << statements[statement].value.text
                      << '\n';
        }
    }

    std::set<std::string> dependencies;
    collect_wildcard_identifiers(statements, dependencies);
    for (const auto& dependency : dependencies) {
        if (const auto found = signals_.find(dependency);
            found != signals_.end()
            && design_.signal_info_[found->second].width != 0U) {
            process_.static_sensitivity.push_back(
                { found->second, runtime::simir::EdgeKind::any });
        }
    }
    std::vector<std::pair<std::size_t, std::size_t>>
        lowered_statement_ranges;
    lowered_statement_ranges.reserve(statement_order.size());
    for (const auto statement_index : statement_order) {
        const auto& statement = statements[statement_index];
        const auto begin = process_.operations.size();
        emit_debug_point(DebugPointKind::process_entry, statement.span);
        lower_statement(statement);
        lowered_statement_ranges.emplace_back(
            begin, process_.operations.size());
    }
    const auto recurring_entry = hoisted_static_assignments == 0U
        ? InstructionIndex { 0U }
        : static_cast<InstructionIndex>(
            hoisted_static_assignments < lowered_statement_ranges.size()
                ? lowered_statement_ranges[hoisted_static_assignments].first
                : process_.operations.size());
    if (forwarding_acyclic) {
        std::unordered_map<SignalId, std::vector<std::size_t>> writers;
        std::vector<std::unordered_set<SignalId>> reads(
            lowered_statement_ranges.size());
        bool reorderable = true;
        for (std::size_t block = 0;
             block < lowered_statement_ranges.size(); ++block) {
            const auto [begin, end] = lowered_statement_ranges[block];
            for (auto index = begin; index < end; ++index) {
                const auto& operation = process_.operations[index];
                if (const auto* read = operation_get_if<ReadSignal>(&operation);
                    read != nullptr
                    && read->kind == SignalReadKind::current) {
                    reads[block].insert(read->signal);
                } else if (const auto* whole_write
                    = operation_get_if<WriteUpdate>(&operation)) {
                    writers[whole_write->signal].push_back(block);
                } else if (const auto* slice_write
                    = operation_get_if<WriteUpdateSlice>(&operation)) {
                    writers[slice_write->signal].push_back(block);
                } else if (const auto* dynamic_write
                    = operation_get_if<WriteUpdateDynamicSlice>(&operation)) {
                    writers[dynamic_write->signal].push_back(block);
                } else if (const auto* dynamic_part_write
                    = operation_get_if<WriteUpdateDynamicPartSlice>(
                        &operation)) {
                    writers[dynamic_part_write->signal].push_back(block);
                }
            }
            if (!reorderable) {
                break;
            }
        }
        std::vector<std::unordered_set<std::size_t>> successors(
            lowered_statement_ranges.size());
        std::vector<std::size_t> indegree(
            lowered_statement_ranges.size());
        if (reorderable) {
            for (std::size_t reader = 0;
                 reader < reads.size(); ++reader) {
                for (const auto signal : reads[reader]) {
                    const auto found = writers.find(signal);
                    if (found == writers.end()) {
                        continue;
                    }
                    if (std::ranges::find(found->second, reader)
                        != found->second.end()) {
                        // Packed storage bridges for statically selected
                        // unpacked elements read the whole backing signal and
                        // write one range in the same statement. Their exact
                        // element ordering was already established by the HIR
                        // dependency pass; adding signal-wide edges here would
                        // manufacture an all-to-all cycle between disjoint
                        // elements.
                        continue;
                    }
                    for (const auto writer : found->second) {
                        if (successors[writer].insert(reader).second) {
                            ++indegree[reader];
                        }
                    }
                    if (!reorderable) {
                        break;
                    }
                }
                if (!reorderable) {
                    break;
                }
            }
        }
        const auto* reachability_filter
            = std::getenv("FSIM_PROFILE_FUSED_REACHABILITY");
        if (reorderable && reachability_filter != nullptr
            && (std::string_view { reachability_filter } == "1"
                || process_.name.find(reachability_filter)
                    != std::string::npos)) {
            for (const auto& sensitivity : process_.static_sensitivity) {
                if (writers.contains(sensitivity.signal)) {
                    continue;
                }
                std::vector<bool> reachable(reads.size(), false);
                std::vector<std::size_t> pending;
                for (std::size_t block = 0; block < reads.size(); ++block) {
                    if (reads[block].contains(sensitivity.signal)) {
                        reachable[block] = true;
                        pending.push_back(block);
                    }
                }
                for (std::size_t next = 0; next < pending.size(); ++next) {
                    for (const auto successor : successors[pending[next]]) {
                        if (!reachable[successor]) {
                            reachable[successor] = true;
                            pending.push_back(successor);
                        }
                    }
                }
                std::size_t operation_count { };
                for (const auto block : pending) {
                    operation_count += lowered_statement_ranges[block].second
                        - lowered_statement_ranges[block].first;
                }
                std::cerr << "fsim-profile: fused-reachability name='"
                          << process_.name << "' signal="
                          << static_cast<std::size_t>(sensitivity.signal)
                          << " blocks=" << pending.size() << '/'
                          << reads.size() << " operations=" << operation_count
                          << '/' << process_.operations.size() << '\n';
            }
        }
        std::vector<std::size_t> exact_order;
        exact_order.reserve(lowered_statement_ranges.size());
        if (reorderable) {
            std::vector<bool> emitted(lowered_statement_ranges.size());
            while (exact_order.size() < lowered_statement_ranges.size()) {
                std::optional<std::size_t> next;
                for (std::size_t candidate = 0;
                     candidate < lowered_statement_ranges.size(); ++candidate) {
                    if (!emitted[candidate]
                        && indegree[candidate] == 0U) {
                        next = candidate;
                        break;
                    }
                }
                if (!next) {
                    reorderable = false;
                    break;
                }
                emitted[*next] = true;
                exact_order.push_back(*next);
                for (const auto successor : successors[*next]) {
                    --indegree[successor];
                }
            }
        }
        if (reorderable) {
            auto lowered = std::move(process_.operations);
            process_.operations.clear();
            process_.operations.reserve(lowered.size());
            std::vector<InstructionIndex> old_to_new(
                lowered.size() + 1U,
                std::numeric_limits<InstructionIndex>::max());
            struct RelocatedBlock {
                std::size_t old_begin;
                std::size_t old_end;
                std::size_t new_begin;
                std::size_t new_end;
            };
            std::vector<RelocatedBlock> relocated_blocks;
            relocated_blocks.reserve(exact_order.size());
            for (const auto block : exact_order) {
                const auto [begin, end] = lowered_statement_ranges[block];
                const auto new_begin = process_.operations.size();
                for (auto index = begin; index < end; ++index) {
                    old_to_new[index] = static_cast<InstructionIndex>(
                        process_.operations.size());
                    process_.operations.push_back(
                        std::move(lowered[index]));
                }
                relocated_blocks.push_back(RelocatedBlock {
                    begin,
                    end,
                    new_begin,
                    process_.operations.size()
                });
            }
            old_to_new[lowered.size()] = static_cast<InstructionIndex>(
                process_.operations.size());
            const auto remap_instruction = [&](const InstructionIndex old) {
                return old < old_to_new.size()
                        && old_to_new[old]
                            != std::numeric_limits<InstructionIndex>::max()
                    ? old_to_new[old]
                    : old;
            };
            for (const auto& block : relocated_blocks) {
                const auto remap_block_instruction
                    = [&](const InstructionIndex old) {
                          if (old >= block.old_begin
                              && old <= block.old_end) {
                              return static_cast<InstructionIndex>(
                                  block.new_begin
                                  + (old - block.old_begin));
                          }
                          return remap_instruction(old);
                      };
                for (auto index = block.new_begin;
                     index < block.new_end; ++index) {
                    auto& operation = process_.operations[index];
                    if (auto* jump = operation_get_if<Jump>(&operation)) {
                        jump->target = remap_block_instruction(jump->target);
                    } else if (auto* call
                        = operation_get_if<Call>(&operation)) {
                        call->target = remap_block_instruction(call->target);
                        call->return_target = remap_block_instruction(
                            call->return_target);
                    } else if (auto* branch
                        = operation_get_if<Branch>(&operation)) {
                        branch->when_true = remap_block_instruction(
                            branch->when_true);
                        branch->when_false = remap_block_instruction(
                            branch->when_false);
                    }
                }
            }
            const auto remap_sites = [&](auto& frames) {
                for (auto& frame : frames) {
                    for (auto& site : frame.call_sites) {
                        site = remap_instruction(site);
                    }
                    for (auto& site : frame.invocation_push_sites) {
                        site = remap_instruction(site);
                    }
                    if (frame.target) {
                        frame.target = remap_instruction(*frame.target);
                    }
                }
            };
            remap_sites(function_frames_);
            remap_sites(task_frames_);
            remap_sites(procedure_frames_);
        } else {
            forwarding_acyclic = false;
        }
    }
    // Large generated continuous-assignment banks are normally acyclic
    // combinational dataflow (XOR trees, generated reductions, decoder tables,
    // and similar structures). Preserve every scheduled signal write for
    // hierarchy/debug visibility, but forward an earlier assignment's value to
    // later reads in the same fused evaluation. Without this elaboration-time
    // inlining, a generated chain is forced through one scheduler delta per
    // intermediate net even though all of its expressions are already in one
    // process. The conservative size threshold keeps small hand-written
    // feedback networks on their exact delta-by-delta path.
    std::unordered_set<SignalId> forwarded_assignment_signals;
    std::unordered_set<SignalId> unforwarded_assignment_reads;
    if (forwarding_acyclic) {
        auto lowered = std::move(process_.operations);
        process_.operations.clear();
        process_.operations.reserve(lowered.size() + statements.size());
        std::unordered_map<
            SignalId,
            std::vector<std::pair<std::uint32_t, std::uint32_t>>>
            static_slice_regions;
        std::unordered_set<SignalId> non_slice_update_signals;
        for (const auto& operation : lowered) {
            if (const auto* slice
                = operation_get_if<WriteUpdateSlice>(&operation)) {
                if (slice->source < register_widths_.size()
                    && register_widths_[slice->source]
                        <= std::numeric_limits<std::uint32_t>::max()) {
                    static_slice_regions[slice->signal].emplace_back(
                        slice->offset,
                        static_cast<std::uint32_t>(
                            register_widths_[slice->source]));
                } else {
                    non_slice_update_signals.insert(slice->signal);
                }
            } else if (const auto* whole
                = operation_get_if<WriteUpdate>(&operation)) {
                non_slice_update_signals.insert(whole->signal);
            } else if (const auto* dynamic
                = operation_get_if<WriteUpdateDynamicSlice>(&operation)) {
                non_slice_update_signals.insert(dynamic->signal);
            } else if (const auto* dynamic_part
                = operation_get_if<WriteUpdateDynamicPartSlice>(&operation)) {
                non_slice_update_signals.insert(dynamic_part->signal);
            }
        }
        std::unordered_set<SignalId> fully_covered_slice_signals;
        constexpr std::size_t maximum_coalesced_slice_word_updates = 512U;
        for (auto& [signal, regions] : static_slice_regions) {
            if (regions.size() < 2U
                || non_slice_update_signals.contains(signal)
                || signal >= design_.signal_info_.size()) {
                continue;
            }
            std::ranges::sort(regions);
            std::uint64_t covered { };
            bool contiguous = true;
            for (const auto& [offset, width] : regions) {
                if (width == 0U || offset != covered) {
                    contiguous = false;
                    break;
                }
                covered += width;
            }
            const auto words = static_cast<std::size_t>((covered + 63U) / 64U);
            if (contiguous
                && covered == design_.signal_info_[signal].width
                && words != 0U
                && regions.size()
                    <= maximum_coalesced_slice_word_updates / words) {
                fully_covered_slice_signals.insert(signal);
            }
        }
        std::vector<InstructionIndex> remapped_boundaries;
        remapped_boundaries.reserve(lowered.size() + 1U);
        std::unordered_map<SignalId, RegisterId> forwarded_values;
        for (const auto& operation : lowered) {
            if (const auto* whole_write
                = operation_get_if<WriteUpdate>(&operation)) {
                forwarded_assignment_signals.insert(whole_write->signal);
            } else if (const auto* slice_write
                = operation_get_if<WriteUpdateSlice>(&operation)) {
                forwarded_assignment_signals.insert(slice_write->signal);
            } else if (const auto* dynamic_write
                = operation_get_if<WriteUpdateDynamicSlice>(&operation)) {
                forwarded_assignment_signals.insert(dynamic_write->signal);
            } else if (const auto* dynamic_part_write
                = operation_get_if<WriteUpdateDynamicPartSlice>(&operation)) {
                forwarded_assignment_signals.insert(
                    dynamic_part_write->signal);
            }
        }
        std::unordered_set<SignalId> forwarding_read_signals;
        for (const auto& operation : lowered) {
            const auto* read = operation_get_if<ReadSignal>(&operation);
            if (read != nullptr
                && read->kind == SignalReadKind::current) {
                forwarding_read_signals.insert(read->signal);
            }
        }
        std::unordered_map<
            InstructionIndex,
            std::unordered_map<SignalId, RegisterId>> forwarded_at_targets;
        const auto merge_target_values = [&](const InstructionIndex target) {
            const auto found = forwarded_at_targets.find(target);
            if (found == forwarded_at_targets.end()) {
                forwarded_at_targets.emplace(target, forwarded_values);
                return;
            }
            std::erase_if(
                found->second,
                [&](const auto& entry) {
                    const auto current = forwarded_values.find(entry.first);
                    return current == forwarded_values.end()
                        || current->second != entry.second;
                });
        };
        const auto current_value = [&](const SignalId signal) {
            const auto found = forwarded_values.find(signal);
            if (found != forwarded_values.end()) {
                return found->second;
            }
            const auto& info = design_.signal_info_[signal];
            const auto value = allocate_register(
                info.width, info.source_domain);
            process_.operations.emplace_back(ReadSignal { value, signal });
            forwarded_values.emplace(signal, value);
            return value;
        };
        bool previous_falls_through { true };
        for (std::size_t old_index = 0;
             old_index < lowered.size(); ++old_index) {
            auto& operation = lowered[old_index];
            if (const auto incoming = forwarded_at_targets.find(
                    static_cast<InstructionIndex>(old_index));
                incoming != forwarded_at_targets.end()) {
                if (previous_falls_through) {
                    std::erase_if(
                        forwarded_values,
                        [&](const auto& entry) {
                            const auto other = incoming->second.find(
                                entry.first);
                            return other == incoming->second.end()
                                || other->second != entry.second;
                        });
                } else {
                    forwarded_values = incoming->second;
                }
            }
            remapped_boundaries.push_back(static_cast<InstructionIndex>(
                process_.operations.size()));
            if (const auto* read = operation_get_if<ReadSignal>(&operation);
                read != nullptr
                && read->kind == SignalReadKind::current) {
                const auto forwarded = forwarded_values.find(read->signal);
                if (forwarded != forwarded_values.end()) {
                    process_.operations.emplace_back(CopyRegister {
                        read->destination, forwarded->second });
                    continue;
                }
                if (forwarded_assignment_signals.contains(read->signal)) {
                    unforwarded_assignment_reads.insert(read->signal);
                }
                forwarded_values.insert_or_assign(
                    read->signal, read->destination);
            } else if (const auto* whole_write
                = operation_get_if<WriteUpdate>(&operation)) {
                forwarded_values.insert_or_assign(
                    whole_write->signal, whole_write->source);
            } else if (const auto* slice_write
                = operation_get_if<WriteUpdateSlice>(&operation)) {
                if (forwarding_read_signals.contains(slice_write->signal)
                    || fully_covered_slice_signals.contains(
                        slice_write->signal)) {
                    const auto& info
                        = design_.signal_info_[slice_write->signal];
                    const auto value = allocate_register(
                        info.width, info.source_domain);
                    process_.operations.emplace_back(Insert {
                        value,
                        current_value(slice_write->signal),
                        slice_write->source,
                        slice_write->offset });
                    forwarded_values.insert_or_assign(
                        slice_write->signal, value);
                } else {
                    forwarded_values.erase(slice_write->signal);
                }
            } else if (const auto* dynamic_write
                = operation_get_if<WriteUpdateDynamicSlice>(&operation)) {
                if (forwarding_read_signals.contains(dynamic_write->signal)) {
                    const auto& info
                        = design_.signal_info_[dynamic_write->signal];
                    const auto value = allocate_register(
                        info.width, info.source_domain);
                    process_.operations.emplace_back(DynamicInsert {
                        value,
                        current_value(dynamic_write->signal),
                        dynamic_write->source,
                        dynamic_write->selection });
                    forwarded_values.insert_or_assign(
                        dynamic_write->signal, value);
                } else {
                    forwarded_values.erase(dynamic_write->signal);
                }
            } else if (const auto* dynamic_part_write
                = operation_get_if<WriteUpdateDynamicPartSlice>(&operation)) {
                if (forwarding_read_signals.contains(
                        dynamic_part_write->signal)) {
                    const auto& info
                        = design_.signal_info_[dynamic_part_write->signal];
                    const auto value = allocate_register(
                        info.width, info.source_domain);
                    process_.operations.emplace_back(DynamicPartInsert {
                        value,
                        current_value(dynamic_part_write->signal),
                        dynamic_part_write->source,
                        dynamic_part_write->selection });
                    forwarded_values.insert_or_assign(
                        dynamic_part_write->signal, value);
                } else {
                    forwarded_values.erase(dynamic_part_write->signal);
                }
            }
            process_.operations.push_back(std::move(operation));
            const auto& emitted = process_.operations.back();
            if (const auto* branch = operation_get_if<Branch>(&emitted)) {
                merge_target_values(branch->when_true);
                merge_target_values(branch->when_false);
                previous_falls_through = false;
            } else if (const auto* jump = operation_get_if<Jump>(&emitted)) {
                merge_target_values(jump->target);
                previous_falls_through = false;
            } else if (operation_holds<Call>(emitted)
                || operation_holds<Return>(emitted)) {
                forwarded_values.clear();
                previous_falls_through = false;
            } else {
                previous_falls_through = true;
            }
        }
        remapped_boundaries.push_back(static_cast<InstructionIndex>(
            process_.operations.size()));
        std::vector<std::pair<SignalId, RegisterId>> coalesced_updates;
        for (const auto signal : fully_covered_slice_signals) {
            const auto value = forwarded_values.find(signal);
            if (value == forwarded_values.end()) {
                continue;
            }
            std::size_t replaced { };
            for (auto& operation : process_.operations) {
                const auto* slice
                    = operation_get_if<WriteUpdateSlice>(&operation);
                if (slice == nullptr || slice->signal != signal) {
                    continue;
                }
                operation = CopyRegister { slice->source, slice->source };
                ++replaced;
            }
            if (replaced >= 2U) {
                coalesced_updates.emplace_back(signal, value->second);
            }
        }
        std::ranges::sort(coalesced_updates);
        const auto remap_instruction = [&](const InstructionIndex old) {
            return old < remapped_boundaries.size()
                ? remapped_boundaries[old]
                : old;
        };
        for (auto& operation : process_.operations) {
            if (auto* jump = operation_get_if<Jump>(&operation)) {
                jump->target = remap_instruction(jump->target);
            } else if (auto* call = operation_get_if<Call>(&operation)) {
                call->target = remap_instruction(call->target);
                call->return_target = remap_instruction(call->return_target);
            } else if (auto* branch = operation_get_if<Branch>(&operation)) {
                branch->when_true = remap_instruction(branch->when_true);
                branch->when_false = remap_instruction(branch->when_false);
            }
        }
        const auto remap_sites = [&](auto& frames) {
            for (auto& frame : frames) {
                for (auto& site : frame.call_sites) {
                    site = remap_instruction(site);
                }
                for (auto& site : frame.invocation_push_sites) {
                    site = remap_instruction(site);
                }
                if (frame.target) {
                    frame.target = remap_instruction(*frame.target);
                }
            }
        };
        remap_sites(function_frames_);
        remap_sites(task_frames_);
        remap_sites(procedure_frames_);
        for (const auto& [signal, source] : coalesced_updates) {
            process_.operations.emplace_back(WriteUpdate { signal, source });
        }
        const auto* forwarding_filter
            = std::getenv("FSIM_PROFILE_FUSED_FORWARDING");
        if (!coalesced_updates.empty() && forwarding_filter != nullptr
            && (std::string_view { forwarding_filter } == "1"
                || process_.name.find(forwarding_filter)
                    != std::string::npos)) {
            std::cerr << "fsim-profile: fused-coalesced name='" << process_.name
                      << "' signals=" << coalesced_updates.size() << '\n';
        }
    }
    for (const auto dependency : implicit_signal_dependencies_) {
        if (design_.signal_info_[dependency].width != 0U) {
            process_.static_sensitivity.push_back(
                { dependency, runtime::simir::EdgeKind::any });
        }
    }
    if (forwarding_acyclic && unforwarded_assignment_reads.empty()) {
        std::erase_if(
            process_.static_sensitivity,
            [&](const Sensitivity& sensitivity) {
                return forwarded_assignment_signals.contains(
                    sensitivity.signal);
            });
    }
    std::ranges::sort(
        process_.static_sensitivity,
        { },
        [](const Sensitivity& sensitivity) {
            return std::pair {
                sensitivity.signal,
                static_cast<std::underlying_type_t<EdgeKind>>(
                    sensitivity.edge)
            };
        });
    process_.static_sensitivity.erase(
        std::ranges::unique(process_.static_sensitivity).begin(),
        process_.static_sensitivity.end());
    // Call-containing fused banks cannot use value forwarding because their
    // callable bodies are appended after the statement stream. They can still
    // avoid evaluating unrelated statements: every source statement retains a
    // contiguous lowered range, and its current-value ReadSignal operations
    // identify the exact static inputs needed for that activation. Calls are
    // deliberately assigned the full sensitivity mask because a callable may
    // reference enclosing signals which are not present among its actuals.
    if (!forwarding_acyclic
        && lowered_statement_ranges.size() >= minimum_forwarded_group_size
        && !process_.static_sensitivity.empty()
        && process_.static_sensitivity.size() <= 63U) {
        const auto full_dependency_mask
            = (UINT64_C(1) << process_.static_sensitivity.size()) - 1U;
        std::unordered_map<SignalId, std::uint64_t> sensitivity_masks;
        for (std::size_t index = 0;
             index < process_.static_sensitivity.size(); ++index) {
            sensitivity_masks[process_.static_sensitivity[index].signal]
                |= UINT64_C(1) << index;
        }
        const auto requires_full_trigger = [&](const auto& self,
                                               const Expression& expression)
            -> bool {
            if (expression.kind == ExpressionKind::Call
                && expression.text != "?:") {
                if (language != frontend::Language::Vhdl2008) {
                    return true;
                }
                const auto found = function_indices_.find(expression.text);
                if (found == function_indices_.end()
                    || !std::ranges::all_of(
                        found->second,
                        [&](const std::size_t function) {
                            return function < function_frames_.size()
                                && function_frames_[function].source != nullptr
                                && function_frames_[function].source->pure;
                        })) {
                    return true;
                }
            }
            return std::ranges::any_of(
                    expression.operands,
                    [&](const Expression& operand) {
                        return self(self, operand);
                    });
        };
        for (std::size_t block = 0;
             block < lowered_statement_ranges.size(); ++block) {
            const auto [begin, end] = lowered_statement_ranges[block];
            std::uint64_t mask { };
            for (auto instruction = begin; instruction < end; ++instruction) {
                const auto* read = operation_get_if<ReadSignal>(
                    &process_.operations[instruction]);
                if (read == nullptr
                    || read->kind != SignalReadKind::current) {
                    continue;
                }
                if (const auto found = sensitivity_masks.find(read->signal);
                    found != sensitivity_masks.end()) {
                    mask |= found->second;
                }
            }
            const auto source_statement = statement_order[block];
            if (requires_full_trigger(
                    requires_full_trigger,
                    statements[source_statement].value)) {
                mask = full_dependency_mask;
            }
            if (!process_.static_trigger_regions.empty()
                && process_.static_trigger_regions.back().end == begin
                && process_.static_trigger_regions.back().mask == mask) {
                process_.static_trigger_regions.back().end
                    = static_cast<InstructionIndex>(end);
            } else {
                process_.static_trigger_regions.push_back(
                    Process::StaticTriggerRegion {
                        static_cast<InstructionIndex>(begin),
                        static_cast<InstructionIndex>(end),
                        mask });
            }
        }
    }
    if (!process_.static_sensitivity.empty()) {
        process_.operations.emplace_back(WaitSensitivity { });
        process_.operations.emplace_back(Jump { recurring_entry });
    } else {
        process_.operations.emplace_back(Halt { });
    }
    do {
        lower_pending_tasks();
        lower_pending_procedures();
        lower_pending_functions();
    } while (!pending_tasks_.empty()
        || !pending_procedures_.empty()
        || !pending_functions_.empty());
    validate_read_only_signal_writes(statements.front().span);
    validate_vhdl_driver_attributes(statements.front().span);
    process_.register_count = next_register_;
    process_.string_register_count = next_string_register_;
    process_.container_register_count = next_container_register_;
    process_.register_value_kinds.reserve(register_domains_.size());
    for (const auto domain : register_domains_) {
        process_.register_value_kinds.push_back(value_kind(domain));
    }
    process_.driver_regions = collect_driver_regions(
        process_, register_widths_);
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
    block_controls_.clear();
    named_block_controls_.clear();
    named_fork_controls_.clear();
    return std::move(process_);
}

std::vector<SignalId> Lowerer::concurrent_sensitivity(
    const Statement& statement) const
{
    auto dependency_probe = statement;
    dependency_probe.target = Expression {
        ExpressionKind::IntegerLiteral, "0", { }, statement.target.span
    };
    if ((statement.target.kind == ExpressionKind::Index
            || statement.target.kind == ExpressionKind::Slice)
        && statement.target.operands.size() > 1U) {
        dependency_probe.task_arguments.insert(
            dependency_probe.task_arguments.end(),
            std::next(statement.target.operands.begin()),
            statement.target.operands.end());
    }
    std::set<std::string> dependencies;
    collect_wildcard_identifiers(
        std::span<const Statement> { &dependency_probe, 1U }, dependencies);
    std::vector<SignalId> result;
    result.reserve(dependencies.size());
    for (const auto& dependency : dependencies) {
        if (const auto found = signals_.find(dependency);
            found != signals_.end()
            && design_.signal_info_[found->second].width != 0U) {
            result.push_back(found->second);
        }
    }
    std::ranges::sort(result);
    result.erase(std::ranges::unique(result).begin(), result.end());
    return result;
}

bool Lowerer::concurrent_trigger_fusion_safe(
    const Statement& statement) const
{
    const auto safe_expression = [&](const auto& self,
                                     const Expression& expression) -> bool {
        if (expression.kind == ExpressionKind::Call
            && expression.text != "?:") {
            bool found_match = false;
            for (const auto& function : functions_) {
                if (function.name != expression.text) {
                    continue;
                }
                found_match = true;
                if (!function.pure) {
                    return false;
                }
            }
            if (!found_match) {
                return false;
            }
        }
        return std::ranges::all_of(
            expression.operands,
            [&](const Expression& operand) {
                return self(self, operand);
            });
    };
    const auto safe_statement = [&](const auto& self,
                                    const Statement& candidate) -> bool {
        if (!safe_expression(safe_expression, candidate.condition)
            || !safe_expression(safe_expression, candidate.value)) {
            return false;
        }
        return std::ranges::all_of(
                   candidate.statements,
                   [&](const Statement& nested) {
                       return self(self, nested);
                   })
            && std::ranges::all_of(
                candidate.else_statements,
                [&](const Statement& nested) {
                    return self(self, nested);
                });
    };
    return safe_statement(safe_statement, statement);
}

} // namespace fsim::elaboration
