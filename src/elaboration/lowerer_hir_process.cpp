// SPDX-License-Identifier: Apache-2.0
#include "lowerer_driver_regions.hpp"
#include "lowerer_internal.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <tuple>

namespace fsim::elaboration {
namespace {

void relocate_instruction_targets(
    Operation& operation,
    const InstructionIndex insertion)
{
    const auto relocate = [insertion](InstructionIndex& target) {
        if (target >= insertion) {
            ++target;
        }
    };
    if (auto* jump = operation_get_if<Jump>(&operation)) {
        relocate(jump->target);
        return;
    }
    if (auto* branch = operation_get_if<Branch>(&operation)) {
        relocate(branch->when_true);
        relocate(branch->when_false);
        return;
    }
    if (auto* call = operation_get_if<Call>(&operation)) {
        if (call->target != 0U) {
            relocate(call->target);
        }
        relocate(call->return_target);
        return;
    }
    if (auto* wait = operation_get_if<WaitOn>(&operation)) {
        if (wait->timeout_origin) {
            relocate(*wait->timeout_origin);
        }
        return;
    }
    if (auto* fork = operation_get_if<Fork>(&operation)) {
        for (auto& target : fork->branches) {
            relocate(target);
        }
        return;
    }
    if (auto* disable = operation_get_if<DisableFork>(&operation)) {
        if (disable->site) {
            relocate(*disable->site);
        }
        return;
    }
    if (auto* disable = operation_get_if<DisableBlock>(&operation)) {
        relocate(disable->begin);
        relocate(disable->end);
    }
}

} // namespace

Lowerer::Lowerer(
    ElaboratedDesign& design,
    const SignalBindings& signals,
    const ReadOnlySignalBindings& read_only_signals,
    const StringObjectBindings& string_objects,
    const ReadOnlyStringBindings& read_only_string_objects,
    const ContainerObjectBindings& container_objects,
    const ReadOnlyContainerBindings& read_only_container_objects,
    std::vector<Diagnostic>& diagnostics)
    : design_(design)
    , signals_(signals)
    , read_only_signals_(read_only_signals)
    , string_objects_(string_objects)
    , read_only_string_objects_(read_only_string_objects)
    , container_objects_(container_objects)
    , read_only_container_objects_(read_only_container_objects)
    , diagnostics_(diagnostics)
{
}

void Lowerer::set_systemverilog_program_owner(
    const std::optional<std::uint32_t> owner) noexcept
{
    systemverilog_program_owner_ = owner;
}

void Lowerer::set_hir_code_coverage_context(
    const CoverageHirContext* coverage, const bool module) noexcept
{
    coverage_hir_context_ = module ? coverage : nullptr;
}

void Lowerer::set_hir_code_coverage_active(const bool active) noexcept
{
    coverage_hir_process_active_ = active;
}

std::vector<Process> Lowerer::take_generated_processes()
{
    return std::exchange(generated_processes_, { });
}

RegisterId Lowerer::allocate_register(
    const std::size_t width,
    const frontend::ValueDomain domain)
{
    const auto id = next_register_++;
    register_widths_.push_back(width);
    register_domains_.push_back(domain);
    return id;
}

RegisterId Lowerer::load_constant(
    PackedLogic4 value, const frontend::ValueDomain domain)
{
    const auto target = allocate_register(value.width(), domain);
    process_.operations.emplace_back(LoadConstant { target, std::move(value) });
    return target;
}

RegisterId Lowerer::load_integer(const std::int64_t value,
    const std::size_t width, const frontend::ValueDomain domain)
{
    return load_constant(integer_value(value, width), domain);
}

StringRegisterId Lowerer::allocate_string_register()
{
    return next_string_register_++;
}

ContainerRegisterId Lowerer::allocate_container_register(
    const ContainerType& type)
{
    const auto id = next_container_register_++;
    process_.container_register_types.push_back(type);
    return id;
}

std::size_t Lowerer::register_width(const RegisterId id) const
{
    return register_widths_.at(static_cast<std::size_t>(id));
}

frontend::ValueDomain Lowerer::register_domain(
    const RegisterId id) const
{
    return register_domains_.at(static_cast<std::size_t>(id));
}

RegisterId Lowerer::resize_register(
    const RegisterId source,
    const std::size_t width,
    const bool sign_extend)
{
    const auto source_width = register_width(source);
    if (source_width == width) {
        return source;
    }
    const auto domain = register_domain(source);
    const auto destination = allocate_register(width, domain);
    if (width < source_width) {
        process_.operations.emplace_back(Extract {
            destination,
            source,
            0,
            static_cast<std::uint32_t>(width),
        });
        return destination;
    }
    const auto extension_width = width - source_width;
    RegisterId extension { };
    if (sign_extend) {
        extension = allocate_register(1U, domain);
        process_.operations.emplace_back(Extract {
            extension,
            source,
            static_cast<std::uint32_t>(source_width - 1U),
            1U,
        });
    } else {
        extension = allocate_register(
            extension_width, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            extension,
            unsigned_value(0U, extension_width),
        });
    }
    std::vector<RegisterId> operands;
    if (sign_extend) {
        operands.assign(extension_width, extension);
    } else {
        operands.push_back(extension);
    }
    operands.push_back(source);
    process_.operations.emplace_back(Concatenate {
        destination,
        std::move(operands),
        static_cast<std::uint32_t>(width),
    });
    return destination;
}

RegisterId Lowerer::convert_to_two_state(const RegisterId source)
{
    if (is_two_state_domain(register_domain(source))) {
        return source;
    }
    const auto destination = allocate_register(
        register_width(source), frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(ConvertToTwoState {
        destination,
        source,
    });
    return destination;
}

void Lowerer::initialize_delayed_net_driver(const SignalId signal,
    const std::size_t offset, const std::size_t width,
    const bool continuous_assignment)
{
    // A net driven by a delayed continuous assignment reads X until the
    // first delayed value matures (IEEE 1800-2017 10.3.3): its undriven
    // (Z) bits start unknown, without a value change at time zero.
    if (!continuous_assignment || signal >= design_.signals_.size()) {
        return;
    }
    auto& initial = design_.signals_[signal].initial_value;
    for (auto bit = offset; bit < offset + width && bit < initial.width();
        ++bit) {
        if (initial.get(bit) == runtime::Logic4::z) {
            initial.set(bit, runtime::Logic4::x);
        }
    }
}

void Lowerer::report(
    std::string code,
    std::string message,
    frontend::SourceSpan span)
{
    diagnostics_.push_back({
        std::move(code), std::move(message), std::move(span) });
}

bool Lowerer::signal_binding_can_emit_write(
    const HirRuntimeBinding& binding) const noexcept
{
    if (binding.kind == HirRuntimeBindingKind::local) {
        return binding.local.has_value();
    }
    if (!binding.signal || !read_only_signals_.contains(*binding.signal)) {
        return binding.signal.has_value();
    }
    return binding.vhdl_port;
}

bool Lowerer::signal_binding_is_writable(
    const HirRuntimeBinding& binding) const noexcept
{
    if (!binding.signal
        || (binding.vhdl_port
            && binding.vhdl_direction
                == semantic::vhdl::Direction::input)) {
        return false;
    }
    if (!read_only_signals_.contains(*binding.signal)) {
        return true;
    }
    return binding.vhdl_port
        && (binding.vhdl_direction == semantic::vhdl::Direction::output
            || binding.vhdl_direction == semantic::vhdl::Direction::inout
            || binding.vhdl_direction == semantic::vhdl::Direction::buffer);
}

void Lowerer::record_readonly_signal_write(const SignalId signal)
{
    if (!read_only_signals_.contains(signal)
        || process_.operations.empty()) {
        return;
    }
    // A read-only exception belongs to exactly this appended operation.
    readonly_signal_write_operations_.insert_or_assign(
        process_.operations.size() - 1U, signal);
}

void Lowerer::record_readonly_vhdl_output_write(
    const HirRuntimeBinding& binding)
{
    if (!binding.signal
        || !read_only_signals_.contains(*binding.signal)
        || !signal_binding_is_writable(binding)) {
        return;
    }
    record_readonly_signal_write(*binding.signal);
}

namespace {

    // The signal a signal-writing operation updates.
    std::optional<SignalId> written_signal(const Operation& operation)
    {
        std::optional<SignalId> result;
        fsim::runtime::simir::visit_operation(
            [&](const auto& candidate) {
                using Operation = std::decay_t<decltype(candidate)>;
                if constexpr (
                    std::is_same_v<Operation, WriteBlocking>
                    || std::is_same_v<Operation, WriteUpdate>
                    || std::is_same_v<Operation, WriteAfter>
                    || std::is_same_v<Operation, WriteInertial>
                    || std::is_same_v<Operation, WriteDelayed>
                    || std::is_same_v<Operation, WriteProjected>
                    || std::is_same_v<Operation, WriteProjectedWaveform>
                    || std::is_same_v<Operation, WriteBlockingSlice>
                    || std::is_same_v<Operation, WriteUpdateSlice>
                    || std::is_same_v<Operation, WriteAfterSlice>
                    || std::is_same_v<Operation, WriteInertialSlice>
                    || std::is_same_v<Operation, WriteProjectedSlice>
                    || std::is_same_v<
                        Operation, WriteProjectedWaveformSlice>
                    || std::is_same_v<Operation, WriteBlockingDynamicSlice>
                    || std::is_same_v<Operation, WriteUpdateDynamicSlice>
                    || std::is_same_v<Operation, WriteAfterDynamicSlice>
                    || std::is_same_v<Operation, WriteInertialDynamicSlice>
                    || std::is_same_v<Operation, WriteProjectedDynamicSlice>
                    || std::is_same_v<
                        Operation, WriteProjectedWaveformDynamicSlice>
                    || std::is_same_v<
                        Operation, WriteBlockingDynamicPartSlice>
                    || std::is_same_v<
                        Operation, WriteUpdateDynamicPartSlice>
                    || std::is_same_v<Operation, WriteAfterDynamicPartSlice>
                    || std::is_same_v<
                        Operation, WriteInertialDynamicPartSlice>
                    || std::is_same_v<Operation, ReleaseSignalSlice>
                    || std::is_same_v<Operation, ForceSignalSlice>) {
                    result = candidate.signal;
                }
            },
            operation);
        return result;
    }

} // namespace

void Lowerer::authorize_readonly_signal_writes(const std::size_t begin)
{
    for (auto index = begin; index < process_.operations.size(); ++index) {
        const auto signal = written_signal(process_.operations[index]);
        if (signal && read_only_signals_.contains(*signal)) {
            readonly_signal_write_operations_.insert_or_assign(
                index, *signal);
        }
    }
}

void Lowerer::validate_read_only_signal_writes(
    const frontend::SourceSpan& source)
{
    std::set<SignalId> reported;
    for (std::size_t operation_index { };
        operation_index < process_.operations.size(); ++operation_index) {
        const auto signal = written_signal(
            process_.operations[operation_index]);
        if (!signal || !read_only_signals_.contains(*signal)) {
            continue;
        }
        const auto authorized_write
            = readonly_signal_write_operations_.find(operation_index);
        const bool permitted_write
            = authorized_write != readonly_signal_write_operations_.end()
            && authorized_write->second == *signal;
        if (!permitted_write && reported.insert(*signal).second) {
            report(
                "FSIM-ELAB-SVIFACE-006",
                "an input signal is read-only within process '"
                    + process_.name + "'",
                source);
        }
    }
}

std::string Lowerer::debug_scope_name() const
{
    auto result = process_.name;
    for (const auto& scope : local_scope_) {
        if (!result.empty()) {
            result += '.';
        }
        result += scope;
    }
    return result;
}

std::vector<std::string> Lowerer::hir_named_scope_path() const
{
    std::vector<std::string> path;
    std::size_t start = 0U;
    while (start <= hierarchy_.size()) {
        const auto dot = hierarchy_.find('.', start);
        const auto stop = dot == std::string::npos ? hierarchy_.size() : dot;
        if (stop > start) {
            path.push_back(hierarchy_.substr(start, stop - start));
        }
        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1U;
    }
    for (const auto& scope : local_scope_) {
        if (!scope.empty() && scope.front() != '$') {
            path.push_back(scope);
        }
    }
    return path;
}

void Lowerer::emit_disable_scope_marker(
    const DebugPointKind kind,
    const std::string_view label,
    const frontend::SourceSpan& span,
    const bool task)
{
    if (disable_target_labels_ == nullptr
        || !disable_target_labels_->contains(std::string { label })) {
        return;
    }
    std::string scope;
    if (task) {
        scope = hierarchy_ + "." + std::string { label };
    } else {
        for (const auto& segment : hir_named_scope_path()) {
            if (!scope.empty()) {
                scope += '.';
            }
            scope += segment;
        }
    }
    process_.operations.emplace_back(DebugPoint {
        kind,
        runtime_source_location(span),
        scope,
    });
}

void Lowerer::emit_debug_point(
    const DebugPointKind kind,
    const frontend::SourceSpan& span)
{
    process_.operations.emplace_back(DebugPoint {
        kind,
        runtime_source_location(span),
        debug_scope_name(),
    });
}

namespace {

frontend::ProcessKind process_kind(
    const semantic::sv::ProcessKind kind)
{
    switch (kind) {
    case semantic::sv::ProcessKind::always:
        return frontend::ProcessKind::VerilogAlways;
    case semantic::sv::ProcessKind::always_ff:
        return frontend::ProcessKind::SystemVerilogAlwaysFF;
    case semantic::sv::ProcessKind::always_comb:
        return frontend::ProcessKind::SystemVerilogAlwaysComb;
    case semantic::sv::ProcessKind::always_latch:
        return frontend::ProcessKind::SystemVerilogAlwaysLatch;
    case semantic::sv::ProcessKind::initial:
        return frontend::ProcessKind::Initial;
    case semantic::sv::ProcessKind::final:
        return frontend::ProcessKind::Final;
    }
    return frontend::ProcessKind::VerilogAlways;
}

PackedLogic4 default_hir_value(
    const std::size_t width,
    const frontend::ValueDomain domain,
    const std::optional<frontend::IntegerRange>& integer_range)
{
    if (domain == frontend::ValueDomain::Logic9) {
        return PackedLogic4::from_logic9_msb_string(
            std::string(width, 'U'));
    }
    if (domain == frontend::ValueDomain::Integer) {
        const auto value = integer_range
            ? integer_range->left
            : width == 64U
            ? std::numeric_limits<std::int64_t>::min()
            : static_cast<std::int64_t>(
                  std::numeric_limits<std::int32_t>::min());
        return integer_value(value, width);
    }
    return PackedLogic4(
        width,
        domain == frontend::ValueDomain::Bit2
                || domain == frontend::ValueDomain::Boolean
            ? Logic4::zero
            : Logic4::x);
}

runtime::simir::EdgeKind edge_kind(const semantic::sv::EdgeKind edge)
{
    switch (edge) {
    case semantic::sv::EdgeKind::positive:
        return runtime::simir::EdgeKind::posedge;
    case semantic::sv::EdgeKind::negative:
        return runtime::simir::EdgeKind::negedge;
    case semantic::sv::EdgeKind::any:
        break;
    }
    return runtime::simir::EdgeKind::any;
}

runtime::simir::StrengthRank strength_rank(const std::uint8_t encoded)
{
    using Frontend = frontend::VerilogStrength;
    using Runtime = runtime::simir::StrengthRank;
    switch (static_cast<Frontend>(encoded)) {
    case Frontend::HighZ:
        return Runtime::highz;
    case Frontend::Small:
        return Runtime::small;
    case Frontend::Medium:
        return Runtime::medium;
    case Frontend::Weak:
        return Runtime::weak;
    case Frontend::Large:
        return Runtime::large;
    case Frontend::Pull:
        return Runtime::pull;
    case Frontend::Strong:
        return Runtime::strong;
    case Frontend::Supply:
        return Runtime::supply;
    }
    return Runtime::strong;
}

} // namespace

std::vector<SignalId> Lowerer::hir_signal_dependencies(
    const semantic::ExpressionId expression,
    const semantic::ScopeId process_scope) const
{
    std::vector<SignalId> dependencies;
    std::unordered_set<std::uint32_t> visited;
    const auto collect = [&](const auto& self,
                             const semantic::ExpressionId current) -> bool {
        if (!current.valid()) {
            return false;
        }
        if (!visited.insert(current.value()).second) {
            return true;
        }
        if (const auto signal = hir_direct_signal(current)) {
            dependencies.push_back(*signal);
        }
        if (const auto declaration = hir_referenced_declaration(current)) {
            const auto binding = hir_runtime_binding(
                *declaration, process_scope, false);
            if (binding && binding->signal) {
                dependencies.push_back(*binding->signal);
            }
        }
        if (const auto member = hir_virtual_interface_member(current)) {
            // A virtual interface member may change in any instance the
            // handle can name, and when the handle itself is reassigned.
            for (const auto& [identity, signal] : member->candidates) {
                (void)identity;
                dependencies.push_back(signal);
            }
            const auto binding = hir_runtime_binding(
                member->receiver, process_scope, false);
            if (binding && binding->signal) {
                dependencies.push_back(*binding->signal);
            }
        }
        if (const auto container = hir_container_object_binding(current);
            container && !container->local) {
            bool element_alias = false;
            for (const auto& alias :
                design_.container_element_signal_aliases_) {
                if (alias.object == container->object && alias.readable) {
                    dependencies.push_back(alias.signal);
                    element_alias = true;
                }
            }
            // Without element nets, the container's bridge signal carries
            // every element change.
            if (!element_alias) {
                const auto alias = std::ranges::find_if(
                    design_.container_signal_aliases_.rbegin(),
                    design_.container_signal_aliases_.rend(),
                    [&](const runtime::simir::ContainerSignalAlias& candidate) {
                        return candidate.object == container->object
                            && candidate.readable;
                    });
                if (alias != design_.container_signal_aliases_.rend()) {
                    dependencies.push_back(alias->signal);
                }
            }
        }
        const auto record = specialized_hir_unit_ != nullptr
            ? specialized_hir_unit_->find_expression(current)
            : std::nullopt;
        if (!record) {
            return false;
        }
        // A member of an unpacked structure (`u.a`) is stored as its own
        // signal.
        if (record->systemverilog != nullptr
            && record->systemverilog->kind
                == semantic::sv::ExpressionKind::name
            && record->systemverilog->text.find('.') != std::string::npos) {
            if (const auto found = signals_.find(record->systemverilog->text);
                found != signals_.end()) {
                dependencies.push_back(found->second);
            } else if (const auto root = signals_.find(
                           record->systemverilog->text.substr(0U,
                               record->systemverilog->text.find_first_of(
                                   ".[")));
                root != signals_.end()) {
                // A member of a bridged unpacked structure changes with the
                // structure's bridge signal.
                dependencies.push_back(root->second);
            }
        }
        std::span<const semantic::ExpressionId> operands;
        if (record->systemverilog != nullptr) {
            operands = record->systemverilog->operands;
            for (const auto& association :
                record->systemverilog->associations) {
                for (const auto choice : association.choices) {
                    if (!self(self, choice)) {
                        return false;
                    }
                }
                if (!self(self, association.value)) {
                    return false;
                }
            }
            for (const auto& argument :
                record->systemverilog->call_arguments) {
                if (argument.actual && !self(self, *argument.actual)) {
                    return false;
                }
            }
        } else {
            operands = record->vhdl->operands;
            for (const auto& association : record->vhdl->associations) {
                for (const auto choice : association.choices) {
                    if (!self(self, choice)) {
                        return false;
                    }
                }
                if (!self(self, association.value)) {
                    return false;
                }
            }
        }
        return std::ranges::all_of(
            operands,
            [&](const semantic::ExpressionId operand) {
                return self(self, operand);
            });
    };
    if (!collect(collect, expression)) {
        return { };
    }
    std::ranges::sort(dependencies);
    auto duplicate = std::ranges::unique(dependencies);
    dependencies.erase(duplicate.begin(), duplicate.end());
    return dependencies;
}

std::vector<SignalId> Lowerer::hir_statement_signal_dependencies(
    const std::span<const semantic::StatementId> statements,
    const semantic::ScopeId process_scope) const
{
    if (specialized_hir_unit_ == nullptr) {
        return { };
    }
    std::vector<SignalId> dependencies;
    std::unordered_set<std::uint32_t> visited_statements;
    std::unordered_set<std::uint32_t> visited_callables;
    const auto append_expression = [&](
                                       const std::optional<semantic::ExpressionId>
                                           expression) {
        if (!expression) {
            return;
        }
        auto expression_dependencies = hir_signal_dependencies(
            *expression, process_scope);
        dependencies.insert(
            dependencies.end(),
            expression_dependencies.begin(),
            expression_dependencies.end());
    };
    const auto append_delay = [&](const auto& self,
                                  const semantic::sv::Delay& delay) -> void {
        append_expression(delay.primary.expression);
        if (delay.minimum) {
            append_expression(delay.minimum->expression);
        }
        if (delay.typical) {
            append_expression(delay.typical->expression);
        }
        if (delay.maximum) {
            append_expression(delay.maximum->expression);
        }
        for (const auto& additional : delay.additional) {
            self(self, additional);
        }
    };
    const auto collect = [&](const auto& self,
                             const semantic::StatementId statement_id)
        -> void {
        if (!statement_id.valid()
            || !visited_statements.insert(statement_id.value()).second) {
            return;
        }
        const auto statement = specialized_hir_unit_->find_statement(
            statement_id);
        if (!statement || statement->systemverilog == nullptr) {
            return;
        }
        const auto& source = *statement->systemverilog;
        append_expression(source.value);
        append_expression(source.condition);
        append_expression(source.loop_initial);
        append_expression(source.loop_limit);
        append_expression(source.loop_update_target);
        append_expression(source.verilog_switch_source);
        append_expression(source.verilog_switch_control);
        append_expression(source.file_handle);
        if (source.delay) {
            append_delay(append_delay, *source.delay);
        }
        for (const auto& sensitivity : source.sensitivities) {
            append_expression(sensitivity.expression);
        }
        for (const auto& argument : source.task_arguments) {
            append_expression(argument.actual);
        }
        for (const auto& output : source.output_values) {
            append_expression(output.value);
        }
        for (const auto& alternative : source.case_alternatives) {
            for (const auto choice : alternative.choices) {
                append_expression(choice);
            }
            for (const auto child : alternative.statements) {
                self(self, child);
            }
        }
        if (source.target) {
            const auto target = specialized_hir_unit_->find_expression(
                *source.target);
            if (target && target->systemverilog != nullptr
                && target->systemverilog->operands.size() > 1U
                && (target->systemverilog->kind
                        == semantic::sv::ExpressionKind::index
                    || target->systemverilog->kind
                        == semantic::sv::ExpressionKind::slice)) {
                for (const auto selector : std::span {
                         target->systemverilog->operands }
                                               .subspan(1U)) {
                    append_expression(selector);
                }
            }
        }
        if (source.kind == semantic::sv::StatementKind::task_call) {
            auto selected = source.task.selected;
            if (!selected && source.task.overloads.size() == 1U) {
                selected = source.task.overloads.front();
            }
            if (selected
                && visited_callables.insert(selected->value()).second) {
                const auto declaration
                    = specialized_hir_unit_->find_declaration(*selected);
                if (declaration
                    && declaration->systemverilog != nullptr
                    && declaration->systemverilog->callable) {
                    for (const auto child :
                        declaration->systemverilog->statements) {
                        self(self, child);
                    }
                }
            }
        }
        for (const auto child : source.loop_updates) {
            self(self, child);
        }
        for (const auto child : source.statements) {
            self(self, child);
        }
        for (const auto child : source.else_statements) {
            self(self, child);
        }
    };
    for (const auto statement : statements) {
        collect(collect, statement);
    }
    std::ranges::sort(dependencies);
    const auto duplicate = std::ranges::unique(dependencies);
    dependencies.erase(duplicate.begin(), duplicate.end());
    return dependencies;
}

std::optional<std::size_t> Lowerer::hir_enclosing_callable_frame(
    const HirCallableFrame& frame) const
{
    if (!frame.enclosing_callable_invocation_identity) {
        return std::nullopt;
    }
    const auto parent = std::ranges::find(hir_callable_frames_,
        *frame.enclosing_callable_invocation_identity,
        &HirCallableFrame::invocation_identity);
    return parent == hir_callable_frames_.end()
        ? std::nullopt
        : std::optional { static_cast<std::size_t>(
              parent - hir_callable_frames_.begin()) };
}

bool Lowerer::hir_retained_static_local(
    const semantic::CompiledDeclarationView& declaration,
    const bool first_initialization) const
{
    return active_hir_callable_
        && !hir_callable_frames_[*active_hir_callable_].type.automatic
        && !first_initialization && declaration.systemverilog != nullptr
        && declaration.systemverilog->lifetime
            != semantic::sv::Lifetime::automatic;
}

bool Lowerer::initialize_hir_declarations(
    const std::span<const semantic::DeclarationId> declarations)
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto debug_name = [&](const semantic::DeclarationId declaration_id,
                                const std::string_view name) {
        if (!active_hir_callable_) {
            std::string result;
            for (const auto& scope_name : local_scope_) {
                if (!result.empty()) {
                    result += ".";
                }
                result += scope_name;
            }
            if (!result.empty()) {
                result += ".";
            }
            result += name;
            (void)declaration_id;
            return result;
        }
        const auto& frame = hir_callable_frames_[*active_hir_callable_];
        const auto base = frame.debug_name.empty()
            ? "@hir-callable-"
                + std::to_string(frame.declaration.value()) + "::"
                + std::string { name }
            : frame.debug_name + "." + std::string { name };
        auto result = base;
        std::size_t occurrence { };
        while (!debug_local_names_.emplace(result).second) {
            result = base + "@hir-callable-"
                + std::to_string(frame.invocation_identity)
                + "-declaration-" + std::to_string(declaration_id.value())
                + "-" + std::to_string(occurrence++);
        }
        return result;
    };
    const auto capture_scalar_subtype_range = [&](
        const semantic::CompiledDeclarationView& declaration) {
        if (declaration.vhdl == nullptr
            || declaration.vhdl->form
                != semantic::vhdl::DeclarationForm::subtype
            || !declaration.vhdl->subtype) {
            return true;
        }
        const auto scalar_range = std::ranges::find_if(
            declaration.vhdl->subtype->constraints,
            [](const semantic::vhdl::RangeConstraint& range) {
                return range.kind == semantic::vhdl::RangeKind::integer
                    || range.kind == semantic::vhdl::RangeKind::discrete;
            });
        if (scalar_range
                == declaration.vhdl->subtype->constraints.end()
            || (scalar_range->left && scalar_range->right)) {
            return true;
        }
        const auto typed_range_scope = declaration.vhdl->scope.valid()
            ? declaration.vhdl->scope
            : hir_process_scope_;
        const auto saved_process_scope = hir_process_scope_;
        hir_process_scope_ = typed_range_scope;
        const auto effective_subtype = hir_effective_vhdl_subtype(
            *declaration.vhdl->subtype);
        hir_process_scope_ = saved_process_scope;

        const bool real_subtype
            = hir_vhdl_subtype_is_real(*declaration.vhdl->subtype)
            || (effective_subtype
                && hir_vhdl_subtype_is_real(*effective_subtype));
        const bool integer_subtype = effective_subtype
            && effective_subtype->domain
                == semantic::vhdl::ValueDomain::integer
            && !real_subtype;
        const bool typed_noninteger_subtype = real_subtype
            || (effective_subtype
                && effective_subtype->domain
                    != semantic::vhdl::ValueDomain::unknown
                && !integer_subtype);
        if (typed_noninteger_subtype) {
            // This capture table is consumed by INTEGER defaults and the
            // callable INTEGER range checker. For other scalar domains,
            // retain the typed subtype range and prove that its bounds are
            // static with the corresponding VHDL evaluator instead of
            // coercing values into INTEGER registers. Unknown/dynamic bounds
            // remain fail-closed below.
            const semantic::vhdl::RangeConstraint* typed_range
                = &*scalar_range;
            if (effective_subtype && scalar_range->source.valid()) {
                const auto materialized = std::ranges::find(
                    effective_subtype->constraints,
                    scalar_range->source,
                    &semantic::vhdl::RangeConstraint::source);
                if (materialized != effective_subtype->constraints.end()) {
                    typed_range = &*materialized;
                }
            }
            const auto bound_is_static = [&](
                const std::optional<std::int64_t> folded,
                const std::optional<semantic::ExpressionId> expression) {
                if (folded) {
                    return true;
                }
                if (!expression) {
                    return false;
                }
                auto expression_scope = typed_range_scope;
                const auto record
                    = specialized_hir_unit_->find_expression(*expression);
                if (record && record->vhdl != nullptr
                    && record->vhdl->scope.valid()) {
                    expression_scope = record->vhdl->scope;
                }
                if (real_subtype) {
                    if (hir_vhdl_constant_real(*expression)) {
                        return true;
                    }
                    const auto attribute = hir_vhdl_attribute_profile(
                        *expression, expression_scope);
                    return attribute && attribute->constant
                        && attribute->width == 64U
                        && attribute->domain
                            == frontend::ValueDomain::Bit2;
                }
                const auto saved_scope = hir_process_scope_;
                hir_process_scope_ = expression_scope;
                const bool constant
                    = hir_constant_integer(*expression).has_value();
                const bool character_literal = record
                    && record->vhdl != nullptr
                    && (record->vhdl->kind
                            == semantic::vhdl::ExpressionKind::logic_literal
                        || (record->vhdl->kind
                                == semantic::vhdl::ExpressionKind::name
                            && hir_vhdl_character_literal_code(
                                *expression, 8U)));
                const bool character_static = !constant
                    && effective_subtype
                    && hir_vhdl_predefined_character_subtype(
                        *effective_subtype, typed_range_scope)
                    && character_literal;
                hir_process_scope_ = saved_scope;
                return constant || character_static;
            };
            const bool bounds_are_static
                = bound_is_static(
                      typed_range->left,
                      typed_range->left_expression)
                && bound_is_static(
                      typed_range->right,
                      typed_range->right_expression);
            const auto descending = typed_range->direction_from_bounds
                ? hir_vhdl_range_direction(*typed_range, typed_range_scope)
                : std::optional<bool> { typed_range->descending };
            if (bounds_are_static && descending) {
                // No captured register is needed here: consumers continue to
                // read the original, nominally typed subtype constraints.
                // Direction was resolved from its recorded source, never
                // inferred from endpoint order.
                return true;
            }
            const auto span = hir_source_span(
                scalar_range->source.valid()
                    ? scalar_range->source
                    : declaration.vhdl->source);
            report("FSIM-ELAB-HIR-001",
                "cannot evaluate both dynamic scalar bounds when VHDL "
                "subtype '" + declaration.vhdl->name
                    + "' is elaborated",
                span);
            return false;
        }
        if (!scalar_range->source.valid()) {
            const auto span = hir_source_span(declaration.vhdl->source);
            report("FSIM-ELAB-HIR-001",
                "cannot capture a dynamic scalar range for VHDL subtype '"
                    + declaration.vhdl->name
                    + "' because its source range has no identity",
                span);
            return false;
        }
        const auto already_captured = std::ranges::any_of(
            hir_vhdl_captured_scalar_ranges_,
            [&](const HirVhdlCapturedScalarRange& captured) {
                return captured.source == scalar_range->source;
            });
        if (already_captured) {
            return true;
        }

        const auto range_span = hir_source_span(scalar_range->source);
        const auto range_scope = declaration.vhdl->scope;
        const auto descending = scalar_range->direction_from_bounds
            ? hir_vhdl_range_direction(*scalar_range, range_scope)
            : std::optional<bool> { scalar_range->descending };
        if (!descending) {
            report("FSIM-ELAB-HIR-001",
                "cannot capture the direction of dynamic scalar range for "
                "VHDL subtype '" + declaration.vhdl->name + "'",
                range_span);
            return false;
        }
        const auto lower_bound = [&](
            const std::optional<std::int64_t> folded,
            const std::optional<semantic::ExpressionId> expression)
            -> std::optional<RegisterId> {
            if (folded) {
                const auto result = load_integer(
                    *folded, 64U, frontend::ValueDomain::Integer);
                return result;
            }
            if (!expression) {
                return std::nullopt;
            }
            auto expression_scope = range_scope;
            if (const auto record
                = specialized_hir_unit_->find_expression(*expression);
                record && record->vhdl != nullptr
                && record->vhdl->scope.valid()) {
                expression_scope = record->vhdl->scope;
            }
            const auto width = hir_expression_width(
                *expression, expression_scope);
            if (!width || *width == 0U || *width > 64U) {
                return std::nullopt;
            }
            const auto saved_scope = hir_process_scope_;
            hir_process_scope_ = expression_scope;
            const auto result = lower_hir_expression(*expression, *width);
            hir_process_scope_ = saved_scope;
            if (!result
                || register_domain(*result)
                    != frontend::ValueDomain::Integer) {
                return std::nullopt;
            }
            // Named scalar bounds are fixed when their subtype declaration
            // elaborates; retain a value snapshot if the expression aliases
            // a mutable local register.
            const auto snapshot = allocate_register(
                register_width(*result), register_domain(*result));
            process_.operations.emplace_back(CopyRegister {
                snapshot, *result });
            return snapshot;
        };
        const auto left = lower_bound(
            scalar_range->left, scalar_range->left_expression);
        const auto right = lower_bound(
            scalar_range->right, scalar_range->right_expression);
        if (!left || !right) {
            report("FSIM-ELAB-HIR-001",
                "cannot evaluate both dynamic scalar bounds when VHDL "
                "subtype '" + declaration.vhdl->name
                    + "' is elaborated",
                range_span);
            return false;
        }
        const HirVhdlCapturedScalarRange captured {
            scalar_range->source,
            *left,
            *right,
            *descending,
        };
        hir_vhdl_captured_scalar_ranges_.push_back(captured);
        if (active_hir_callable_
            && *active_hir_callable_ < hir_callable_frames_.size()) {
            hir_callable_frames_[*active_hir_callable_]
                .body_vhdl_scalar_ranges.push_back(captured);
        }
        return true;
    };
    std::unordered_set<std::string> declared_here;
    for (const auto declaration_id : declarations) {
        const auto declaration = specialized_hir_unit_->find_declaration(
            declaration_id);
        if (!declaration) {
            return false;
        }
        if (!capture_scalar_subtype_range(*declaration)) {
            return false;
        }
        const auto vhdl_loop_parameter = [&] {
            if (declaration->vhdl == nullptr
                || declaration->vhdl->form
                    != semantic::vhdl::DeclarationForm::constant) {
                return false;
            }
            const auto scope = declaration->vhdl->scope;
            const auto& scopes
                = specialized_hir_unit_->design().semantics.scopes();
            return scope.valid() && scope.value() < scopes.size()
                && scopes[scope.value()].name == "<loop>";
        }();
        if (declaration->systemverilog != nullptr) {
            if (hir_static_storage(declaration_id)) {
                continue;
            }
            using Form = semantic::sv::DeclarationForm;
            const auto form = declaration->systemverilog->form;
            if (form != Form::port && form != Form::net
                && form != Form::variable) {
                // Compile-time declarations and nested callables are retained
                // in HIR but do not allocate process-local runtime storage.
                continue;
            }
        } else if (!vhdl_loop_parameter) {
            using Form = semantic::vhdl::DeclarationForm;
            // A local constant of a multi-bit logic vector subtype is held
            // like a variable, so an indexed or sliced read can select from
            // it; scalar constants keep the folding paths.
            const auto vector_constant
                = hir_vhdl_local_vector_constant(*declaration->vhdl);
            if (declaration->vhdl->form != Form::variable
                && declaration->vhdl->form != Form::file && !vector_constant) {
                // Constants, types, aliases, package instances, attributes,
                // and nested subprograms remain available through HIR name
                // resolution. Only variables and file objects own
                // process-local storage.
                continue;
            }
        }
        const auto local_name = declaration->systemverilog != nullptr
            ? declaration->systemverilog->name
            : declaration->vhdl->name;
        const auto local_source = declaration->systemverilog != nullptr
            ? declaration->systemverilog->source
            : declaration->vhdl->source;
        if (!declared_here.emplace(local_name).second) {
            report(
                "FSIM-ELAB-053",
                "duplicate local variable in the same scope '"
                    + local_name + "'",
                hir_source_span(local_source));
            continue;
        }
        // A protected object owns its private variables (IEEE 1076-2008
        // 5.6.2); it is created when its declaration is elaborated.
        if (declaration->vhdl != nullptr
            && declaration->vhdl->form
                == semantic::vhdl::DeclarationForm::variable) {
            if (const auto created = initialize_hir_vhdl_protected_object(
                    declaration_id, *declaration->vhdl)) {
                if (!*created) {
                    return false;
                }
                continue;
            }
        }
        // An unpacked structure or union local is a singleton aggregate
        // container, as at module scope (IEEE 1800-2017 7.2).
        const auto aggregate_local = declaration->systemverilog != nullptr
                && declaration->systemverilog->type
                && !declaration->systemverilog->type->container_form
            ? hir_systemverilog_container_type(
                  *declaration->systemverilog->type)
            : std::nullopt;
        const bool aggregate_value_local = aggregate_local
            && aggregate_local->element_kind
                == ContainerElementKind::Aggregate
            && aggregate_local->aggregate_value;
        // A typedef of an unpacked array, queue or associative array
        // (`typedef logic [7:0] bytes_t []; bytes_t a;`).
        const bool typedef_container_local
            = aggregate_local && !aggregate_value_local && [&] {
                  const auto effective
                      = hir_resolver().effective_systemverilog_type(
                          *declaration->systemverilog->type,
                          declaration->systemverilog->scope);
                  return effective && effective->container_form.has_value();
              }();
        if (declaration->systemverilog != nullptr
            && declaration->systemverilog->type
            && (declaration->systemverilog->type->container_form
                || aggregate_value_local || typedef_container_local)) {
            const auto& input = *declaration->systemverilog;
            const auto type = aggregate_value_local
                ? aggregate_local
                : hir_systemverilog_container_type(*input.type);
            if (!type) {
                return false;
            }
            const auto existing = hir_local_container_registers_.find(
                declaration_id.value());
            const bool first_initialization
                = existing == hir_local_container_registers_.end();
            const auto destination = first_initialization
                ? allocate_container_register(*type)
                : existing->second;
            const bool retained_static
                = hir_retained_static_local(*declaration, first_initialization);
            if (first_initialization) {
                hir_local_container_registers_.emplace(
                    declaration_id.value(), destination);
                hir_local_container_types_.insert_or_assign(
                    declaration_id.value(), *type);
            }
            const auto span = hir_source_span(input.source);
            auto name = debug_name(declaration_id, input.name);
            const bool first_debug_name = active_hir_callable_
                || debug_local_names_.emplace(name).second;
            if (first_initialization && first_debug_name) {
                process_.debug_container_locals.push_back(
                    DebugContainerLocal {
                        std::move(name),
                        destination,
                        *type,
                        runtime_source_location(span),
                    });
            }
            if (input.initializer && !retained_static) {
                const auto initializer
                    = specialized_hir_unit_->find_expression(
                        *input.initializer);
                const auto pattern = initializer
                    && initializer->systemverilog != nullptr
                    && initializer->systemverilog->kind
                        == semantic::sv::ExpressionKind::
                            assignment_pattern;
                // `int a[] = new[n];` sizes a dynamic array with a size
                // known only when the scope is entered (7.5.1).
                const auto* constructor = initializer
                    ? initializer->systemverilog : nullptr;
                std::optional<semantic::ExpressionId> constructed_size;
                if (constructor != nullptr && !type->fixed && !type->queue
                    && !type->associative) {
                    if (constructor->kind == semantic::sv::ExpressionKind::call
                        && constructor->text == "@new-array"
                        && constructor->operands.size() == 1U) {
                        constructed_size = constructor->operands.front();
                    } else if (constructor->kind
                            == semantic::sv::ExpressionKind::index
                        && constructor->operands.size() == 2U) {
                        const auto callee = specialized_hir_unit_
                            ->find_expression(constructor->operands.front());
                        if (callee && callee->systemverilog != nullptr
                            && callee->systemverilog->kind
                                == semantic::sv::ExpressionKind::name
                            && callee->systemverilog->text == "new") {
                            constructed_size = constructor->operands.back();
                        }
                    }
                }
                if (constructed_size) {
                    auto size = lower_hir_expression(*constructed_size, 32U);
                    if (!size) {
                        return false;
                    }
                    if (register_width(*size) != 32U) {
                        size = resize_register(*size, 32U,
                            hir_expression_signed(*constructed_size));
                    }
                    process_.operations.emplace_back(ResizeContainer {
                        destination, *size, std::nullopt, false });
                    continue;
                }
                const auto initializer_expression
                    = specialized_hir_unit_->find_expression(
                        *input.initializer);
                const bool concatenation = initializer_expression
                    && initializer_expression->systemverilog != nullptr
                    && initializer_expression->systemverilog->kind
                        == semantic::sv::ExpressionKind::concatenation
                    && !type->fixed && !type->associative
                    && !type->aggregate_value;
                const auto initial = concatenation
                    ? lower_hir_container_concatenation(*type,
                          initializer_expression->systemverilog->operands)
                    : pattern
                    ? lower_hir_container_assignment_pattern(
                          *input.initializer,
                          HirContainerObjectBinding {
                              declaration_id,
                              input.name,
                              { },
                              destination,
                              &*type,
                              false,
                          })
                    : lower_hir_static_container_actual(
                          *input.initializer, *type);
                if (!initial) {
                    return false;
                }
                process_.operations.emplace_back(CopyContainerRegister {
                    destination, *initial });
            }
            continue;
        }
        const auto vhdl_environment_container_type = [&]()
            -> std::optional<ContainerType> {
            if (declaration->vhdl == nullptr) {
                return std::nullopt;
            }
            if (is_hir_vhdl_environment_directory(declaration_id)) {
                return hir_vhdl_environment_directory_container_type();
            }
            if (is_hir_vhdl_environment_call_path(declaration_id)) {
                return hir_vhdl_environment_call_path_container_type();
            }
            return std::nullopt;
        }();
        if (vhdl_environment_container_type) {
            const auto& type = *vhdl_environment_container_type;
            const auto existing = hir_local_container_registers_.find(
                declaration_id.value());
            const bool first_initialization
                = existing == hir_local_container_registers_.end();
            const auto destination = first_initialization
                ? allocate_container_register(type)
                : existing->second;
            if (first_initialization) {
                hir_local_container_registers_.emplace(
                    declaration_id.value(), destination);
                hir_local_container_types_.insert_or_assign(
                    declaration_id.value(), type);
            }
            const auto span = hir_source_span(
                declaration->vhdl->source);
            auto name = debug_name(
                declaration_id, declaration->vhdl->name);
            const bool first_debug_name = active_hir_callable_
                || debug_local_names_.emplace(name).second;
            if (first_initialization && first_debug_name) {
                process_.debug_container_locals.push_back(
                    DebugContainerLocal {
                        std::move(name),
                        destination,
                        type,
                        runtime_source_location(span),
                    });
            }
            const bool retained_static
                = hir_retained_static_local(*declaration, first_initialization);
            if (is_hir_vhdl_environment_call_path(declaration_id)
                && declaration->vhdl->initializer
                && !retained_static) {
                const auto initial
                    = lower_hir_vhdl_environment_call_path_container_expression(
                        *declaration->vhdl->initializer);
                if (!initial) {
                    return false;
                }
                process_.operations.emplace_back(CopyContainerRegister {
                    destination, *initial });
            }
            continue;
        }
        if (const auto string_binding = hir_string_binding(
                declaration_id, hir_process_scope_, false)) {
            if (string_binding->kind != HirStringBindingKind::local) {
                return false;
            }
            const auto initializer = declaration->systemverilog != nullptr
                ? declaration->systemverilog->initializer
                : declaration->vhdl->initializer;
            const auto source = declaration->systemverilog != nullptr
                ? declaration->systemverilog->source
                : declaration->vhdl->source;
            const auto existing = hir_local_string_registers_.find(
                declaration_id.value());
            const bool first_initialization
                = existing == hir_local_string_registers_.end();
            const auto destination = first_initialization
                ? allocate_string_register()
                : existing->second;
            const bool retained_static
                = hir_retained_static_local(*declaration, first_initialization);
            if (first_initialization) {
                hir_local_string_registers_.emplace(
                    declaration_id.value(), destination);
            }
            string_locals_.insert_or_assign(
                string_binding->name, destination);
            if (!retained_static) {
                bool default_value_initialized { };
                if (declaration->vhdl != nullptr
                    && declaration->vhdl->subtype) {
                    const auto effective = hir_effective_vhdl_subtype(
                        *declaration->vhdl->subtype);
                    if (effective
                        && hir_vhdl_predefined_string_type(*effective)) {
                        std::optional<std::uint64_t> byte_count;
                        if (effective->constraints.size() == 1U) {
                            const auto& range
                                = effective->constraints.front();
                            if (range.kind
                                != semantic::vhdl::RangeKind::array_index) {
                                return false;
                            }
                            const auto left = hir_range_left(range);
                            const auto right = hir_range_right(range);
                            const bool null_range = range.null
                                || (left && right
                                    && (range.descending
                                            ? *left < *right
                                            : *left > *right));
                            if (null_range) {
                                byte_count = 0U;
                            } else if (left && right) {
                                const auto distance
                                    = support::index_distance(*left, *right);
                                if (distance
                                    == std::numeric_limits<
                                        std::uint64_t>::max()) {
                                    return false;
                                }
                                byte_count = distance + 1U;
                            } else {
                                const auto lower_bound = [&](
                                                             const std::optional<std::int64_t> value,
                                                             const std::optional<
                                                                 semantic::ExpressionId>
                                                                 expression)
                                    -> std::optional<RegisterId> {
                                    if (value) {
                                        const auto result
                                            = load_integer(*value, 64U,
                                                frontend::ValueDomain::Integer);
                                        return result;
                                    }
                                    if (!expression) {
                                        return std::nullopt;
                                    }
                                    auto result = lower_hir_expression(
                                        *expression, 64U);
                                    if (!result) {
                                        return std::nullopt;
                                    }
                                    if (register_width(*result) != 64U) {
                                        result = resize_register(*result, 64U,
                                            hir_expression_signed(
                                                *expression));
                                    }
                                    return result;
                                };
                                const auto left_register = lower_bound(
                                    left, range.left_expression);
                                const auto right_register = lower_bound(
                                    right, range.right_expression);
                                if (!left_register || !right_register) {
                                    return false;
                                }
                                const auto non_null = allocate_register(
                                    1U, frontend::ValueDomain::Bit2);
                                process_.operations.emplace_back(Binary {
                                    range.descending
                                        ? BinaryOperator::greater_equal_signed
                                        : BinaryOperator::less_equal_signed,
                                    non_null,
                                    *left_register,
                                    *right_register,
                                });
                                process_.operations.emplace_back(
                                    LoadStringConstant { destination, { } });
                                const auto branch
                                    = static_cast<InstructionIndex>(
                                        process_.operations.size());
                                process_.operations.emplace_back(Branch {
                                    non_null,
                                    static_cast<InstructionIndex>(branch + 1U),
                                    0U,
                                    UnknownBranchPolicy::when_false,
                                });
                                const auto difference = allocate_register(
                                    64U, frontend::ValueDomain::Integer);
                                process_.operations.emplace_back(
                                    IntegerBinary {
                                        IntegerBinaryOperator::subtract,
                                        difference,
                                        range.descending
                                            ? *left_register
                                            : *right_register,
                                        range.descending
                                            ? *right_register
                                            : *left_register,
                                    });
                                const auto one = load_integer(
                                    1, 64U, frontend::ValueDomain::Integer);
                                const auto raw_extent = allocate_register(
                                    64U, frontend::ValueDomain::Integer);
                                process_.operations.emplace_back(
                                    IntegerBinary {
                                        IntegerBinaryOperator::add,
                                        raw_extent,
                                        difference,
                                        one,
                                    });
                                process_.operations.emplace_back(
                                    IntegerCheck {
                                        raw_extent,
                                        0,
                                        static_cast<std::int64_t>(
                                            runtime::simir::
                                                maximum_string_bytes),
                                    });
                                const auto extent32 = resize_register(
                                    raw_extent, 32U, false);
                                const auto one32 = load_integer(
                                    1, 32U, frontend::ValueDomain::Integer);
                                const auto last = allocate_register(
                                    32U, frontend::ValueDomain::Integer);
                                process_.operations.emplace_back(
                                    IntegerBinary {
                                        IntegerBinaryOperator::subtract,
                                        last,
                                        extent32,
                                        one32,
                                    });
                                const auto seed = allocate_string_register();
                                process_.operations.emplace_back(
                                    LoadStringConstant {
                                        seed,
                                        std::string(
                                            runtime::simir::
                                                maximum_string_bytes,
                                            '\0'),
                                    });
                                const auto first32 = load_integer(
                                    0, 32U, frontend::ValueDomain::Integer);
                                StringMethod default_string;
                                default_string.operation
                                    = StringMethodOperator::substr;
                                default_string.source = seed;
                                default_string.string_destination
                                    = destination;
                                default_string.first = first32;
                                default_string.second = last;
                                process_.operations.emplace_back(
                                    default_string);
                                const auto end
                                    = static_cast<InstructionIndex>(
                                        process_.operations.size());
                                process_.operations[branch] = Branch {
                                    non_null,
                                    static_cast<InstructionIndex>(branch + 1U),
                                    end,
                                    UnknownBranchPolicy::when_false,
                                };
                                default_value_initialized = true;
                            }
                        } else if (effective->constraints.empty()
                            && effective->executable_width
                            && *effective->executable_width % 8U == 0U) {
                            byte_count = *effective->executable_width / 8U;
                        } else if (!effective->constraints.empty()) {
                            return false;
                        }
                        if (byte_count) {
                            if (*byte_count
                                > runtime::simir::maximum_string_bytes) {
                                return false;
                            }
                            process_.operations.emplace_back(
                                LoadStringConstant {
                                    destination,
                                    std::string(
                                        static_cast<std::size_t>(*byte_count),
                                        '\0'),
                                });
                            default_value_initialized = true;
                        }
                    }
                }
                if (!default_value_initialized) {
                    process_.operations.emplace_back(LoadStringConstant {
                        destination, { } });
                }
            }
            if (initializer && !retained_static) {
                const auto initial = lower_hir_string_expression(
                    *initializer,
                    declaration->vhdl != nullptr
                            && declaration->vhdl->subtype
                        ? &*declaration->vhdl->subtype
                        : nullptr);
                if (!initial) {
                    return false;
                }
                if (declaration->vhdl != nullptr
                    && declaration->vhdl->subtype) {
                    const auto effective = hir_effective_vhdl_subtype(
                        *declaration->vhdl->subtype);
                    if (effective
                        && effective->domain
                            == semantic::vhdl::ValueDomain::string
                        && hir_vhdl_predefined_string_type(*effective)
                        && !effective->constraints.empty()) {
                        emit_hir_vhdl_string_length_check(
                            destination, *initial,
                            hir_source_span(declaration->vhdl->source));
                    }
                }
                process_.operations.emplace_back(CopyStringRegister {
                    destination, *initial });
            }
            const auto span = hir_source_span(source);
            auto name = debug_name(declaration_id, string_binding->name);
            const bool first_debug_name = active_hir_callable_
                || debug_local_names_.emplace(name).second;
            if (first_initialization && first_debug_name) {
                process_.debug_string_locals.push_back(DebugStringLocal {
                    std::move(name),
                    destination,
                    runtime_source_location(span),
                });
            }
            continue;
        }
        auto binding = hir_runtime_binding(
            declaration_id, hir_process_scope_, false);
        if (binding && active_hir_callable_
            && declaration->vhdl != nullptr
            && declaration->vhdl->subtype) {
            const auto& frame
                = hir_callable_frames_[*active_hir_callable_];
            const auto callable = specialized_hir_unit_->find_declaration(
                frame.declaration);
            const auto return_identifier = callable
                    && callable->vhdl != nullptr
                    && callable->vhdl->callable
                ? callable->vhdl->callable->return_identifier
                : std::nullopt;
            auto type_id
                = declaration->vhdl->subtype->type_mark.target;
            std::unordered_set<std::uint32_t> visited;
            bool contextual_result { };
            if (return_identifier) {
                const auto result_declaration
                    = specialized_hir_unit_->find_declaration(
                        *return_identifier);
                auto type_name = std::string_view {
                    declaration->vhdl->subtype->type_mark.spelling
                };
                type_name = simple_name_of(type_name);
                contextual_result = result_declaration
                    && result_declaration->vhdl != nullptr
                    && type_name == result_declaration->vhdl->name;
            }
            while (return_identifier && type_id.valid()
                && !contextual_result
                && visited.insert(type_id.value()).second) {
                const auto type = specialized_hir_unit_->find_type(type_id);
                if (!type || type->vhdl == nullptr) {
                    break;
                }
                if (type->vhdl->declaration == *return_identifier) {
                    contextual_result = true;
                    break;
                }
                type_id = type->vhdl->base.type_mark.target;
            }
            if (contextual_result) {
                binding->width = frame.type.width;
                binding->domain = frame.type.domain;
                binding->signed_value = frame.type.signed_value;
            }
        }
        if (declaration->vhdl != nullptr
            && declaration->vhdl->initializer
            && declaration->vhdl->subtype
            && std::ranges::any_of(
                declaration->vhdl->subtype->constraints,
                [](const semantic::vhdl::RangeConstraint& range) {
                    return range.left_expression
                        || range.right_expression;
                })
            && [&] {
                   // A statically sized declared subtype owns the layout;
                   // the initializer's width only sizes dynamic ranges.
                   const auto declared = hir_effective_vhdl_subtype(
                       *declaration->vhdl->subtype);
                   return !binding || !declared || declared->unconstrained
                       || !declared->executable_width
                       || *declared->executable_width != binding->width;
               }()) {
            const auto contextual_width = hir_expression_width(
                *declaration->vhdl->initializer, hir_process_scope_);
            const auto contextual_domain = hir_expression_domain(
                *declaration->vhdl->initializer, hir_process_scope_);
            if (contextual_width && *contextual_width != 0U
                && contextual_domain
                && *contextual_domain
                    != frontend::ValueDomain::Unknown) {
                HirRuntimeBinding contextual;
                contextual.declaration = declaration_id;
                contextual.kind = HirRuntimeBindingKind::local;
                contextual.name = declaration->vhdl->name;
                contextual.width = *contextual_width;
                contextual.domain = *contextual_domain;
                contextual.signed_value = hir_expression_signed(
                    *declaration->vhdl->initializer);
                binding = std::move(contextual);
            }
        }
        // A global constant computed at run time is elaborated as a local
        // of its first reader (materialize_hir_vhdl_global_constant).
        if (materializing_hir_vhdl_global_constant_
            && (!binding || binding->kind != HirRuntimeBindingKind::local)
            && declaration->vhdl != nullptr
            && declaration->vhdl->form
                == semantic::vhdl::DeclarationForm::file) {
            HirRuntimeBinding local;
            local.declaration = declaration_id;
            local.kind = HirRuntimeBindingKind::local;
            local.name = declaration->vhdl->name;
            local.width = 32U;
            local.domain = frontend::ValueDomain::Bit2;
            binding = std::move(local);
        }
        if (materializing_hir_vhdl_global_constant_
            && (!binding || binding->kind != HirRuntimeBindingKind::local)
            && declaration->vhdl != nullptr && declaration->vhdl->subtype) {
            const auto declared = hir_effective_vhdl_subtype(
                *declaration->vhdl->subtype);
            if (declared && declared->executable_width
                && *declared->executable_width != 0U) {
                HirRuntimeBinding local;
                local.declaration = declaration_id;
                local.kind = HirRuntimeBindingKind::local;
                local.name = declaration->vhdl->name;
                local.width = *declared->executable_width;
                local.domain = vhdl_value_domain(declared->domain);
                local.signed_value = declared->signed_value;
                binding = std::move(local);
            }
        }
        if (!binding || binding->kind != HirRuntimeBindingKind::local) {
            if (declaration->vhdl != nullptr) {
                report(
                    "FSIM-ELAB-HIR-001",
                    "compiled VHDL HIR local '"
                        + declaration->vhdl->name
                        + "' has no process-local runtime binding",
                    hir_source_span(declaration->vhdl->source));
            }
            return false;
        }
        std::string type_name;
        semantic::SourceSpanId source;
        std::optional<semantic::ExpressionId> initializer;
        std::optional<semantic::ExpressionId> file_open_kind;
        std::vector<std::string> enumeration_literals;
        std::optional<std::int64_t> vhdl_enumeration_default;
        std::optional<semantic::vhdl::SubtypeIndication>
            vhdl_effective_subtype;
        std::optional<semantic::vhdl::SubtypeIndication>
            vhdl_initializer_subtype;
        bool vhdl_file { };
        if (declaration->systemverilog != nullptr) {
            const auto& input = *declaration->systemverilog;
            source = input.source;
            initializer = input.initializer;
            if (input.type) {
                type_name = input.type->target.spelling;
            }
        } else {
            const auto& input = *declaration->vhdl;
            source = input.source;
            initializer = input.initializer;
            file_open_kind = input.file_open_kind;
            vhdl_file = input.form
                    == semantic::vhdl::DeclarationForm::file
                || input.object_class == semantic::vhdl::ObjectClass::file;
            if (input.subtype) {
                type_name = input.subtype->type_mark.spelling;
                const auto effective = hir_effective_vhdl_subtype(
                    *input.subtype);
                vhdl_effective_subtype = effective;
                if (input.initializer) {
                    vhdl_initializer_subtype = effective;
                }
                auto type_id = effective
                        && effective->type_mark.target.valid()
                    ? effective->type_mark.target
                    : input.subtype->type_mark.target;
                std::unordered_set<std::uint32_t> visited;
                while (type_id.valid()
                    && visited.insert(type_id.value()).second) {
                    const auto type
                        = specialized_hir_unit_->find_type(type_id);
                    if (!type || type->vhdl == nullptr) {
                        break;
                    }
                    if (!type->vhdl->enumeration_literals.empty()) {
                        enumeration_literals.reserve(
                            type->vhdl->enumeration_literals.size());
                        for (const auto& literal :
                            type->vhdl->enumeration_literals) {
                            enumeration_literals.push_back(
                                literal.spelling);
                        }
                        vhdl_enumeration_default = 0;
                        if (effective && !effective->constraints.empty()) {
                            const auto& constraint
                                = effective->constraints.front();
                            const auto left = constraint.left
                                ? constraint.left
                                : constraint.left_expression
                                ? specialized_hir_unit_
                                      ->evaluate_integral_expression(
                                          *constraint.left_expression)
                                : std::nullopt;
                            if (left) {
                                vhdl_enumeration_default = *left;
                            }
                        }
                        break;
                    }
                    const auto type_declaration
                        = specialized_hir_unit_->find_declaration(
                            type->vhdl->declaration);
                    if (!type_declaration
                        || type_declaration->vhdl == nullptr
                        || !type_declaration->vhdl->subtype
                        || !type_declaration->vhdl->subtype
                                ->type_mark.target.valid()) {
                        break;
                    }
                    type_id = type_declaration->vhdl->subtype
                                  ->type_mark.target;
                }
            }
        }
        const auto existing = hir_local_registers_.find(
            declaration_id.value());
        const bool first_initialization
            = existing == hir_local_registers_.end();
        const auto destination = first_initialization
            ? allocate_register(binding->width, binding->domain)
            : existing->second;
        const bool retained_static
            = hir_retained_static_local(*declaration, first_initialization);
        locals_.insert_or_assign(binding->name, destination);
        if (first_initialization) {
            hir_local_registers_.emplace(
                declaration_id.value(), destination);
        }
        const auto vhdl_captured_scalar_default = [&]()
            -> std::optional<RegisterId> {
            if (declaration->vhdl == nullptr
                || declaration->vhdl->form
                    != semantic::vhdl::DeclarationForm::variable
                || !declaration->vhdl->subtype || initializer
                || binding->domain != frontend::ValueDomain::Integer) {
                return std::nullopt;
            }
            if (!vhdl_effective_subtype
                || vhdl_effective_subtype->domain
                    != semantic::vhdl::ValueDomain::integer) {
                return std::nullopt;
            }
            for (const auto& constraint :
                vhdl_effective_subtype->constraints) {
                if ((constraint.kind
                        != semantic::vhdl::RangeKind::integer
                    && constraint.kind
                        != semantic::vhdl::RangeKind::discrete)
                    || !constraint.source.valid()) {
                    continue;
                }
                const auto captured = std::ranges::find(
                    hir_vhdl_captured_scalar_ranges_.rbegin(),
                    hir_vhdl_captured_scalar_ranges_.rend(),
                    constraint.source,
                    &HirVhdlCapturedScalarRange::source);
                if (captured
                    != hir_vhdl_captured_scalar_ranges_.rend()) {
                    return captured->left;
                }
            }
            return std::nullopt;
        }();
        const auto vhdl_composite_default = declaration->vhdl != nullptr
                && declaration->vhdl->subtype && !initializer && !vhdl_file
            ? hir_vhdl_default_value(*declaration->vhdl->subtype,
                  binding->width)
            : std::nullopt;
        if (!retained_static && binding->width != 0U
            && vhdl_captured_scalar_default) {
            auto initial = *vhdl_captured_scalar_default;
            if (register_domain(initial)
                    != frontend::ValueDomain::Integer) {
                return false;
            }
            if (register_width(initial) != binding->width) {
                initial = resize_register(initial, binding->width, true);
            }
            process_.operations.emplace_back(CopyRegister {
                destination, initial });
        } else if (!retained_static && binding->width != 0U
            && vhdl_composite_default) {
            process_.operations.emplace_back(LoadConstant {
                destination, *vhdl_composite_default });
        } else if (!retained_static && binding->width != 0U) {
            process_.operations.emplace_back(LoadConstant {
                destination,
                vhdl_enumeration_default
                    ? integer_value(
                          *vhdl_enumeration_default, binding->width)
                    : default_hir_value(
                          binding->width, binding->domain,
                          binding->integer_range) });
        }
        if (vhdl_file && initializer && !retained_static) {
            const auto path = lower_hir_string_expression(*initializer);
            const auto kind = file_open_kind
                ? specialized_hir_unit_->find_expression(*file_open_kind)
                : std::nullopt;
            auto kind_name = kind && kind->vhdl != nullptr
                ? std::string_view { kind->vhdl->text }
                : std::string_view { "read_mode" };
            const auto separator = kind_name.find_last_of('.');
            if (separator != std::string_view::npos) {
                kind_name.remove_prefix(separator + 1U);
            }
            const auto mode_text = kind_name == "read_mode"
                ? std::string_view { "r" }
                : kind_name == "write_mode"
                ? std::string_view { "w" }
                : kind_name == "append_mode"
                ? std::string_view { "a" }
                : std::string_view { };
            if (!path || mode_text.empty()) {
                const auto diagnostic_source = kind && kind->vhdl != nullptr
                    ? kind->vhdl->source
                    : source;
                report("FSIM-ELAB-VHFILE-004",
                    "VHDL file open kind must be read_mode, write_mode, "
                    "or append_mode",
                    hir_source_span(diagnostic_source));
                return false;
            }
            const auto mode = allocate_string_register();
            process_.operations.emplace_back(LoadStringConstant {
                mode, std::string { mode_text } });
            process_.operations.emplace_back(FileOpen {
                destination, *path, mode, std::nullopt, true });
        } else if (initializer && !retained_static
            && binding->width != 0U) {
            const auto initializer_expression
                = specialized_hir_unit_->find_expression(*initializer);
            const auto vhdl_access_allocator
                = declaration->vhdl != nullptr
                && declaration->vhdl->subtype
                && initializer_expression
                && initializer_expression->vhdl != nullptr
                && initializer_expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::call
                && (initializer_expression->vhdl->text == "@vhdl-new"
                    || initializer_expression->vhdl->text
                        == "@vhdl-new-qualified");
            const auto vhdl_aggregate
                = declaration->vhdl != nullptr
                && declaration->vhdl->subtype
                && initializer_expression
                && initializer_expression->vhdl != nullptr
                && initializer_expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::aggregate;
            const auto synchronization
                = declaration->systemverilog != nullptr
                    && declaration->systemverilog->type
                    && initializer_expression
                    && initializer_expression->systemverilog != nullptr
                    && initializer_expression->systemverilog->kind
                        == semantic::sv::ExpressionKind::call
                    && initializer_expression->systemverilog->text
                        .starts_with("@sv-sync-new:")
                ? lower_hir_synchronization_expression(
                      *initializer,
                      binding->width,
                      &*declaration->systemverilog->type)
                : HirSynchronizationAttempt { };
            auto initial = synchronization.handled
                ? synchronization.value
                : vhdl_aggregate
                ? lower_hir_vhdl_aggregate(
                      *initializer,
                      binding->width,
                      vhdl_initializer_subtype
                          ? &*vhdl_initializer_subtype
                          : &*declaration->vhdl->subtype)
                : vhdl_access_allocator
                ? lower_hir_vhdl_access_expression(
                      *initializer, binding->width,
                      &*declaration->vhdl->subtype)
                : lower_hir_expression(*initializer, binding->width,
                      frontend::SystemVerilogScalarKind::None,
                      !enumeration_literals.empty() && vhdl_initializer_subtype
                          ? &*vhdl_initializer_subtype
                          : nullptr);
            if (!initial) {
                if (declaration->vhdl != nullptr) {
                    report(
                        "FSIM-ELAB-HIR-001",
                        "compiled VHDL HIR initializer for local '"
                            + declaration->vhdl->name
                            + "' could not be lowered",
                        hir_source_span(declaration->vhdl->source));
                }
                return false;
            }
            if (register_width(*initial) != binding->width) {
                initial = resize_register(
                    *initial, binding->width, binding->signed_value);
            }
            if (binding->domain == frontend::ValueDomain::Integer
                && binding->integer_range) {
                process_.operations.emplace_back(IntegerCheck {
                    *initial,
                    std::min(binding->integer_range->left,
                        binding->integer_range->right),
                    std::max(binding->integer_range->left,
                        binding->integer_range->right),
                });
            }
            process_.operations.emplace_back(
                CopyRegister { destination, *initial });
        }
        const auto span = hir_source_span(source);
        const auto& scopes
            = specialized_hir_unit_->design().semantics.scopes();
        const auto declaration_scope
            = declaration->systemverilog != nullptr
            ? declaration->systemverilog->scope
            : declaration->vhdl->scope;
        const bool loop_control = declaration_scope.valid()
            && declaration_scope.value() < scopes.size()
            && scopes[declaration_scope.value()].name == "<loop>";
        auto name = debug_name(declaration_id, binding->name);
        // A declaration initialized again (a fork's block item in a loop)
        // keeps one debug local.
        const bool first_debug_name = active_hir_callable_
            ? std::ranges::none_of(process_.debug_locals,
                  [&](const DebugLocal& existing) {
                      return existing.name == name;
                  })
            : debug_local_names_.emplace(name).second;
        if (first_initialization && !loop_control && first_debug_name) {
            process_.debug_locals.push_back(DebugLocal {
                std::move(name),
                std::move(type_name),
                destination,
                binding->width,
                runtime_source_location(span),
                binding->integer_range
                    ? std::optional { std::min(
                          binding->integer_range->left,
                          binding->integer_range->right) }
                    : std::nullopt,
                binding->integer_range
                    ? std::optional { std::max(
                          binding->integer_range->left,
                          binding->integer_range->right) }
                    : std::nullopt,
                value_kind(binding->domain),
                std::move(enumeration_literals),
                frontend::SystemVerilogScalarKind::None,
            });
        }
    }
    return true;
}

std::optional<Process> Lowerer::lower_hir_process(
    const semantic::ProcessId process_id,
    const frontend::Language language,
    const std::string_view hierarchy)
{
    [[maybe_unused]] const HirEffectiveVhdlSubtypeCacheScope cache_scope {
        *this
    };
    const auto source = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_process(process_id)
        : std::nullopt;
    if (!source) {
        return std::nullopt;
    }

    HirProcessDescription description;
    if (source->systemverilog != nullptr) {
        const auto& input = *source->systemverilog;
        description.source = input.source;
        description.scope = input.scope;
        description.name = input.name == "<process>"
            ? std::string { }
            : input.name;
        description.declarations = input.declarations;
        description.statements = input.statements;
        description.kind = process_kind(input.kind);
        description.initial
            = input.kind == semantic::sv::ProcessKind::initial;
        description.final
            = input.kind == semantic::sv::ProcessKind::final;
        description.body_timed_always
            = input.kind == semantic::sv::ProcessKind::always
            && input.sensitivities.empty();
        description.always_comb_or_latch = input.kind
                == semantic::sv::ProcessKind::always_comb
            || input.kind == semantic::sv::ProcessKind::always_latch;
        description.start_after_procedures
            = description.always_comb_or_latch;
        description.concurrent_assertion = input.concurrent_assertion;
        for (const auto& sensitivity : input.sensitivities) {
            if (sensitivity.signal == "*") {
                description.wildcard_sensitivity = true;
                continue;
            }
            std::optional<SignalId> signal;
            if (sensitivity.expression
                && sensitivity.expression->valid()) {
                if (sensitivity.edge == semantic::sv::EdgeKind::any) {
                    // An indexed event-array sensitivity denotes the selected
                    // event object, not a packed value dependency.
                    if (const auto event_array
                        = hir_systemverilog_event_array_selection(
                            *sensitivity.expression, input.scope)) {
                        const auto index
                            = specialized_hir_unit_->evaluate_integral_expression(
                                event_array->index_expression);
                        if (index) {
                            const auto selected = std::ranges::find_if(
                                event_array->elements,
                                [&](const auto& element) {
                                    return element.first == *index;
                                });
                            if (selected == event_array->elements.end()) {
                                report(
                                    "FSIM-ELAB-SVEVENT-012",
                                    "constant event-array index is outside its "
                                    "declared range",
                                    hir_source_span(sensitivity.source));
                                return std::nullopt;
                            }
                            description.sensitivities.push_back({
                                selected->second,
                                runtime::simir::EdgeKind::any,
                            });
                            continue;
                        }
                    }
                    if (const auto selected = hir_static_signal_sensitivity(
                            *sensitivity.expression, input.scope)) {
                        // A constant selected element/bit subscribes to its
                        // exact leaf SignalId and normalized bit range.
                        description.sensitivities.push_back(*selected);
                        continue;
                    }
                    const auto sensitivity_expression
                        = specialized_hir_unit_->find_expression(
                            *sensitivity.expression);
                    const bool direct_name
                        = sensitivity_expression
                        && sensitivity_expression->systemverilog != nullptr
                        && sensitivity_expression->systemverilog->kind
                            == semantic::sv::ExpressionKind::name;
                    if (direct_name) {
                        if (const auto container
                            = hir_container_object_binding(
                                *sensitivity.expression)) {
                            bool has_element_alias = false;
                            for (const auto& alias
                                : design_.container_element_signal_aliases_) {
                                if (alias.object == container->object
                                    && alias.readable) {
                                    description.sensitivities.push_back({
                                        alias.signal,
                                        runtime::simir::EdgeKind::any });
                                    has_element_alias = true;
                                }
                            }
                            if (has_element_alias) {
                                continue;
                            }
                        }
                    }
                    if (const auto element = hir_container_element_binding(
                            *sensitivity.expression)) {
                        bool has_element_alias = false;
                        for (const auto& alias
                            : design_.container_element_signal_aliases_) {
                            if (alias.object == element->object
                                && alias.readable) {
                                description.sensitivities.push_back({
                                    alias.signal,
                                    runtime::simir::EdgeKind::any });
                                has_element_alias = true;
                            }
                        }
                        if (has_element_alias) {
                            // Dynamic indices depend on the full element set
                            // plus any scalar signals used to compute the
                            // index. Constant element selects were handled by
                            // hir_static_signal_sensitivity above.
                            for (const auto index : element->indices) {
                                for (const auto dependency
                                    : hir_signal_dependencies(
                                        index, input.scope)) {
                                    description.sensitivities.push_back({
                                        dependency,
                                        runtime::simir::EdgeKind::any });
                                }
                            }
                            continue;
                        }
                    }
                }

                const auto declaration = hir_referenced_declaration(
                    *sensitivity.expression);
                const auto binding = declaration
                    ? hir_runtime_binding(
                          *declaration, input.scope, false)
                    : std::nullopt;
                if (binding) {
                    signal = binding->signal;
                }
                if (!signal && sensitivity.edge == semantic::sv::EdgeKind::any
                    && (description.event_expression
                        || input.sensitivities.size() != 1U)) {
                    // A constant never changes; another expression is
                    // watched through every signal it reads (9.4.2).
                    for (const auto dependency : hir_signal_dependencies(
                             *sensitivity.expression, input.scope)) {
                        description.sensitivities.push_back({
                            dependency, runtime::simir::EdgeKind::any });
                    }
                    continue;
                }
                if (!signal) {
                    if (description.event_expression
                        || input.sensitivities.size() != 1U) {
                        report(
                            "FSIM-ELAB-SVEVENT-002",
                            "packed process event-expression metadata is "
                            "mixed with another event",
                            hir_source_span(sensitivity.source));
                        continue;
                    }
                    const auto width = hir_expression_width(
                        *sensitivity.expression, input.scope);
                    // An edge of a vector expression is detected on its
                    // least significant bit (IEEE 1800-2017 9.4.2).
                    if (!width || *width == 0U
                        || *width > std::numeric_limits<std::uint32_t>::max()) {
                        return std::nullopt;
                    }
                    auto dependencies = hir_signal_dependencies(
                        *sensitivity.expression, input.scope);
                    if (dependencies.empty()) {
                        return std::nullopt;
                    }
                    description.event_expression
                        = *sensitivity.expression;
                    description.event_edge = edge_kind(sensitivity.edge);
                    description.event_expression_scalar_edge
                        = sensitivity.edge
                            != semantic::sv::EdgeKind::any;
                    for (const auto dependency : dependencies) {
                        description.sensitivities.push_back({
                            dependency,
                            runtime::simir::EdgeKind::any,
                        });
                    }
                    continue;
                }
            }
            if (!signal) {
                signal = hir_named_signal(sensitivity.signal, input.scope);
            }
            // A constant (a parameter or enumeration literal) never
            // changes, so it adds no event.
            if (!signal && sensitivity.edge == semantic::sv::EdgeKind::any
                && input.sensitivities.size() > 1U
                && hir_resolver()
                       .resolve_systemverilog_constant(
                           sensitivity.signal, input.scope, false,
                           sensitivity.source_order)
                       .unique()) {
                continue;
            }
            if (!signal) {
                return std::nullopt;
            }
            description.sensitivities.push_back(
                { *signal, edge_kind(sensitivity.edge) });
        }
    } else {
        const auto& input = *source->vhdl;
        description.source = input.source;
        description.scope = input.scope;
        description.name = input.name == "<process>"
            ? std::string { }
            : input.name;
        description.declarations = input.declarations;
        description.statements = input.statements;
        description.postponed = input.postponed;
        description.kind = frontend::ProcessKind::VhdlProcess;
        std::optional<semantic::DeclarationId> edge_signal;
        auto edge = runtime::simir::EdgeKind::any;
        if (input.statements.size() == 1U
            && input.sensitivities.size() == 1U) {
            const auto statement = specialized_hir_unit_->find_statement(
                input.statements.front());
            const auto condition = statement && statement->vhdl != nullptr
                    && statement->vhdl->kind
                        == semantic::vhdl::StatementKind::conditional
                    && statement->vhdl->else_statements.empty()
                    && statement->vhdl->condition
                ? specialized_hir_unit_->find_expression(
                      *statement->vhdl->condition)
                : std::nullopt;
            if (condition && condition->vhdl != nullptr
                && condition->vhdl->kind
                    == semantic::vhdl::ExpressionKind::call
                && condition->vhdl->operands.size() == 1U
                && (condition->vhdl->text == "rising_edge"
                    || condition->vhdl->text == "falling_edge")) {
                edge_signal = hir_referenced_declaration(
                    condition->vhdl->operands.front());
                if (edge_signal) {
                    edge = condition->vhdl->text == "rising_edge"
                        ? runtime::simir::EdgeKind::posedge
                        : runtime::simir::EdgeKind::negedge;
                }
            }
        }
        for (const auto& sensitivity : input.sensitivities) {
            if (sensitivity.signal == "*") {
                description.wildcard_sensitivity = true;
                continue;
            }
            std::optional<SignalId> signal;
            if (sensitivity.expression
                && sensitivity.expression->valid()) {
                const auto declaration = hir_referenced_declaration(
                    *sensitivity.expression);
                const auto binding = declaration
                    ? hir_runtime_binding(
                          *declaration, input.scope, false)
                    : std::nullopt;
                if (binding) {
                    signal = binding->signal;
                }
                if (!signal) {
                    const auto dependencies = hir_signal_dependencies(
                        *sensitivity.expression, input.scope);
                    if (dependencies.empty()) {
                        return std::nullopt;
                    }
                    // A statically selected element or slice of a signal
                    // subscribes to its bits only (IEEE 1076-2008 11.3).
                    if (const auto selection = dependencies.size() == 1U
                            ? hir_root_constant_selection(
                                  *sensitivity.expression, input.scope)
                            : std::nullopt;
                        selection && selection->width != 0U
                        && dependencies.front()
                            < design_.signal_info_.size()
                        && selection->offset + selection->width
                            <= design_.signal_info_[dependencies.front()]
                                   .width) {
                        description.sensitivities.push_back({
                            dependencies.front(),
                            runtime::simir::EdgeKind::any,
                            static_cast<std::uint32_t>(selection->offset),
                            static_cast<std::uint32_t>(selection->width),
                        });
                        continue;
                    }
                    for (const auto dependency : dependencies) {
                        description.sensitivities.push_back({
                            dependency,
                            runtime::simir::EdgeKind::any,
                        });
                    }
                    continue;
                }
            }
            if (!signal) {
                signal = hir_named_signal(sensitivity.signal, input.scope);
            }
            if (!signal) {
                return std::nullopt;
            }
            description.sensitivities.push_back(
                { *signal, runtime::simir::EdgeKind::any });
        }
        const auto edge_binding = edge_signal
            ? hir_runtime_binding(*edge_signal, input.scope, false)
            : std::nullopt;
        if (edge_binding && edge_binding->signal
            && description.sensitivities.size() == 1U
            && description.sensitivities.front().signal
                == *edge_binding->signal) {
            description.sensitivities.front().edge = edge;
            const auto statement = specialized_hir_unit_->find_statement(
                input.statements.front());
            description.statements = statement->vhdl->statements;
            description.wait_before_first_execution = true;
        }
    }

    return lower_hir_process_body(description, language, hierarchy);
}

std::optional<SignalId> Lowerer::hir_concurrent_port_signal(
    const semantic::DeclarationId declaration) const
{
    const auto binding = hir_runtime_binding(
        declaration, semantic::ScopeId { }, false);
    return binding && binding->kind == HirRuntimeBindingKind::signal
        ? binding->signal : std::nullopt;
}

std::optional<std::vector<SystemVerilogTemplateElementSignalBinding>>
resolve_systemverilog_template_element_signals(
    const semantic::DeclarationId declaration,
    const semantic::SpecializedHirOverlay& overlay,
    const ContainerDeclarationBindings& bindings,
    const std::span<const ContainerObjectInfo> object_info,
    const std::span<const ContainerObject> objects,
    const std::span<const ContainerElementSignalAlias> element_aliases,
    const std::span<const ContainerAggregateSignalAlias> aggregate_aliases,
    const std::span<const SignalInfo> signal_info,
    const std::span<const Signal> signals)
{
    const auto declaration_binding = bindings.find(declaration.value());
    if (declaration_binding == bindings.end()) {
        return std::nullopt;
    }
    if (declaration_binding->second.overlay != &overlay) {
        return std::nullopt;
    }
    const auto object_id = declaration_binding->second.object;
    if (object_id >= objects.size()) {
        return std::nullopt;
    }
    const auto info = std::ranges::find_if(
        object_info,
        [&](const ContainerObjectInfo& candidate) {
            return candidate.id == object_id;
        });
    if (info == object_info.end()) {
        return std::nullopt;
    }
    const auto& object = objects[object_id];
    const auto& type = info->type;
    if (info->is_port || info->slice_alias || object.slice_alias
        || object.name != info->name
        || object.initial_value.type != type
        || !type.fixed || type.two_state || type.queue
        || type.associative || type.string_indices
        || type.dimensions.empty()
        || type.element_kind != ContainerElementKind::Packed
        || type.element_width == 0U || !type.element_types.empty()
        || object.initial_value.elements.empty()
        || std::ranges::none_of(
            element_aliases,
            [&](const ContainerElementSignalAlias& alias) {
                return alias.object == object_id;
            })
        || std::ranges::none_of(
            aggregate_aliases,
            [&](const ContainerAggregateSignalAlias& alias) {
                return alias.object == object_id && alias.readable;
            })) {
        return std::nullopt;
    }

    const auto& dimension = type.dimensions.front();
    if (type.index_left != dimension.first
        || type.index_right != dimension.second) {
        return std::nullopt;
    }
    const auto bridge_width = runtime::simir::container_signal_bridge_width(type);
    if (!bridge_width || *bridge_width % type.element_width != 0U
        || *bridge_width / type.element_width != object.initial_value.elements.size()
        || object.initial_value.elements.size()
            > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }

    std::vector<std::optional<SystemVerilogTemplateElementSignalBinding>>
        by_ordinal(object.initial_value.elements.size());
    std::vector<bool> seen_signals(signal_info.size(), false);
    for (const auto& alias : element_aliases) {
        if (alias.object != object_id) {
            continue;
        }
        if (alias.ordinal >= by_ordinal.size()
            || by_ordinal[alias.ordinal]
            || alias.signal >= signals.size()
            || alias.signal >= signal_info.size()
            || (!alias.readable && !alias.writable)
            || seen_signals[alias.signal]) {
            return std::nullopt;
        }
        auto remaining = static_cast<std::uint64_t>(alias.ordinal);
        std::string suffix;
        for (auto dimension_it = type.dimensions.rbegin();
            dimension_it != type.dimensions.rend(); ++dimension_it) {
            const auto [left, right] = *dimension_it;
            const auto extent = static_cast<std::uint64_t>(left >= right
                ? static_cast<std::int64_t>(left) - right
                : static_cast<std::int64_t>(right) - left) + 1U;
            const auto position = remaining % extent;
            remaining /= extent;
            const auto source_index = static_cast<std::int64_t>(left)
                + (left >= right ? -static_cast<std::int64_t>(position)
                                 : static_cast<std::int64_t>(position));
            suffix = "[" + std::to_string(source_index) + "]" + suffix;
        }
        if (remaining != 0U) {
            return std::nullopt;
        }
        const auto expected_name = info->name + suffix;
        const auto& element_info = signal_info[alias.signal];
        const auto& signal = signals[alias.signal];
        if (element_info.id != alias.signal
            || element_info.name != expected_name
            || signal.name != expected_name
            || element_info.width != type.element_width
            || element_info.source_domain != frontend::ValueDomain::Logic4
            || element_info.is_signed != type.signed_elements
            || element_info.systemverilog_scalar
                != frontend::SystemVerilogScalarKind::None
            || element_info.vhdl_array || element_info.vhdl_access
            || element_info.vhdl_physical
            || !element_info.vhdl_mode_view_bindings.empty()
            || signal.value_kind != ValueKind::logic4
            || signal.initial_value.width() != type.element_width
            || signal.initial_value.is_logic9()) {
            return std::nullopt;
        }
        by_ordinal[alias.ordinal]
            = SystemVerilogTemplateElementSignalBinding {
                  type,
                  alias.ordinal,
                  alias.signal,
                  alias.readable,
                  alias.writable,
                  declaration_binding->second.read_only,
              };
        seen_signals[alias.signal] = true;
    }

    std::vector<SystemVerilogTemplateElementSignalBinding> result;
    result.reserve(by_ordinal.size());
    for (const auto& element : by_ordinal) {
        if (!element) {
            return std::nullopt;
        }
        result.push_back(*element);
    }
    return result;
}

std::vector<Lowerer::HirConcurrentContainerElementSignal>
Lowerer::hir_concurrent_container_element_signals(
    const semantic::DeclarationId declaration) const
{
    if (container_declaration_bindings_ == nullptr
        || specialized_hir_unit_ == nullptr) {
        return { };
    }
    const auto resolved = resolve_systemverilog_template_element_signals(
        declaration,
        specialized_hir_unit_->specialization(),
        *container_declaration_bindings_,
        design_.container_object_info_,
        design_.container_objects_,
        design_.container_element_signal_aliases_,
        design_.container_aggregate_signal_aliases_,
        design_.signal_info_,
        design_.signals_);
    return resolved.value_or(
        std::vector<HirConcurrentContainerElementSignal> { });
}

bool Lowerer::has_generated_processes() const noexcept
{
    return !generated_processes_.empty();
}

std::uint32_t Lowerer::next_hir_callable_invocation_identity() const noexcept
{
    return next_callable_invocation_identity_;
}

bool Lowerer::advance_hir_callable_invocation_identity(
    const std::uint32_t expected_before,
    const std::uint32_t after) noexcept
{
    if (next_callable_invocation_identity_ != expected_before
        || after < expected_before) {
        return false;
    }
    next_callable_invocation_identity_ = after;
    return true;
}

bool Lowerer::hir_concurrent_signal_read_only(
    const SignalId signal) const noexcept
{
    return read_only_signals_.contains(signal);
}

bool Lowerer::diagnose_hir_vhdl_block_guard(
    const semantic::ExpressionId expression)
{
    [[maybe_unused]] const HirEffectiveVhdlSubtypeCacheScope cache_scope {
        *this
    };
    const auto source = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(expression)
        : std::nullopt;
    if (!source || source->vhdl == nullptr) {
        return false;
    }
    const auto domain = hir_expression_domain(
        expression, semantic::ScopeId { });
    if (!domain || *domain == frontend::ValueDomain::Boolean) {
        return true;
    }
    report(
        "FSIM-ELAB-GEN-013",
        "VHDL block guard expression must have Boolean type",
        hir_source_span(source->vhdl->source));
    return false;
}

std::optional<Process> Lowerer::lower_hir_concurrent_statement(
    const semantic::StatementId statement,
    const frontend::Language language,
    const std::string_view hierarchy,
    const std::size_t order,
    const std::optional<semantic::ExpressionId> enclosing_vhdl_guard)
{
    [[maybe_unused]] const HirEffectiveVhdlSubtypeCacheScope cache_scope {
        *this
    };
    const auto source = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_statement(statement)
        : std::nullopt;
    if (!source) {
        return std::nullopt;
    }
    const auto source_span = source->systemverilog != nullptr
        ? source->systemverilog->source
        : source->vhdl->source;
    const std::array statements { statement };
    HirProcessDescription description;
    description.source = source_span;
    // The statement's enclosing unit scope is a name-resolution scope,
    // not process-local storage.
    description.scope = semantic::ScopeId { };
    if (source->systemverilog != nullptr
        && !source->systemverilog->label.empty()) {
        description.name = source->systemverilog->label;
    } else if (source->vhdl != nullptr
        && !source->vhdl->label.empty()) {
        description.name = source->vhdl->label;
    } else {
        description.name = "concurrent_" + std::to_string(order);
    }
    description.statements = statements;
    // A postponed concurrent assertion or procedure call is a postponed
    // process (IEEE 1076-2008 11.4, 11.5).
    description.postponed = source->vhdl != nullptr && source->vhdl->postponed;
    description.kind = language == frontend::Language::Vhdl2008
        ? frontend::ProcessKind::VhdlProcess
        : frontend::ProcessKind::SystemVerilogAlwaysComb;
    description.wildcard_sensitivity = true;
    description.require_wildcard_dependency = false;
    description.always_comb_or_latch = true;
    // A concurrent assertion is sensitive to the longest static prefix of
    // each signal name in it (IEEE 1076-2008 10.2, 11.5): a statically
    // selected element subscribes to its bits only.
    if (source->vhdl != nullptr
        && source->vhdl->kind == semantic::vhdl::StatementKind::assertion
        && source->vhdl->condition) {
        std::vector<runtime::simir::Sensitivity> sensitivities;
        bool complete = true;
        const auto collect = [&](const auto& self,
                                 const semantic::ExpressionId id) -> void {
            const auto node = specialized_hir_unit_->find_expression(id);
            if (!complete || !node || node->vhdl == nullptr) {
                complete = false;
                return;
            }
            const auto& expression = *node->vhdl;
            if (expression.kind == semantic::vhdl::ExpressionKind::index
                || expression.kind == semantic::vhdl::ExpressionKind::slice
                || expression.kind == semantic::vhdl::ExpressionKind::call) {
                const auto dependencies = hir_signal_dependencies(
                    id, semantic::ScopeId { });
                if (const auto selection = dependencies.size() == 1U
                        ? hir_root_constant_selection(id, semantic::ScopeId { })
                        : std::nullopt;
                    selection && selection->width != 0U
                    && dependencies.front() < design_.signal_info_.size()
                    && selection->offset + selection->width
                        <= design_.signal_info_[dependencies.front()].width) {
                    sensitivities.push_back({ dependencies.front(),
                        runtime::simir::EdgeKind::any,
                        static_cast<std::uint32_t>(selection->offset),
                        static_cast<std::uint32_t>(selection->width) });
                    return;
                }
            }
            if (expression.kind == semantic::vhdl::ExpressionKind::name) {
                for (const auto dependency : hir_signal_dependencies(
                         id, semantic::ScopeId { })) {
                    sensitivities.push_back(
                        { dependency, runtime::simir::EdgeKind::any });
                }
                return;
            }
            for (const auto operand : expression.operands) {
                self(self, operand);
            }
        };
        collect(collect, *source->vhdl->condition);
        if (complete && !sensitivities.empty()) {
            description.wildcard_sensitivity = false;
            description.sensitivities = std::move(sensitivities);
        }
    }
    if (source->vhdl != nullptr) {
        description.vhdl_guarded_assignment
            = source->vhdl->guarded_assignment;
        description.vhdl_guard = source->vhdl->guard
            ? source->vhdl->guard
            : enclosing_vhdl_guard;
        description.vhdl_disconnection_delay
            = source->vhdl->disconnection_delay;
    }
    auto result = lower_hir_process_body(
        description, language, hierarchy);
    if (result && source->systemverilog != nullptr
        && (source->systemverilog->drive_zero.has_value()
            != source->systemverilog->drive_one.has_value())) {
        return std::nullopt;
    }
    if (result && source->systemverilog != nullptr
        && source->systemverilog->drive_zero
        && source->systemverilog->drive_one) {
        result->drive_strength = {
            strength_rank(*source->systemverilog->drive_zero),
            strength_rank(*source->systemverilog->drive_one),
        };
    }
    const auto* switch_statement = source->systemverilog != nullptr
            && source->systemverilog->verilog_switch_driver
        ? source->systemverilog
        : nullptr;
    if (!result || switch_statement == nullptr) {
        return result;
    }
    const auto endpoint = [&](const semantic::ExpressionId expression)
        -> std::optional<SignalId> {
        const auto declaration = hir_target_declaration(expression);
        const auto binding = declaration
            ? hir_runtime_binding(
                  *declaration, semantic::ScopeId { }, false)
            : std::nullopt;
        return binding ? binding->signal : std::nullopt;
    };
    if (!switch_statement->verilog_switch_source) {
        return std::nullopt;
    }
    result->switch_source = endpoint(
        *switch_statement->verilog_switch_source);
    if (switch_statement->verilog_switch_control) {
        result->switch_control = endpoint(
            *switch_statement->verilog_switch_control);
        if (!result->switch_control) {
            const auto control = specialized_hir_unit_
                ->evaluate_integral_expression(
                    *switch_statement->verilog_switch_control);
            if (!control || (*control != 0 && *control != 1)
                || ((*control != 0)
                    != switch_statement->verilog_switch_active_high)) {
                result->switch_source.reset();
                return result;
            }
        }
        result->switch_active_high
            = switch_statement->verilog_switch_active_high;
    }
    // A MOS source may be any expression, including a literal. Its lowered
    // continuous assignment and static drive strength are complete without
    // topology metadata. Transmission primitives, by contrast, have lvalue
    // terminals and require a concrete source signal for their bidirectional
    // connectivity.
    if (!result->switch_source) {
        return switch_statement->verilog_switch_bidirectional
            ? std::nullopt
            : result;
    }
    result->switch_bidirectional
        = switch_statement->verilog_switch_bidirectional;
    result->switch_resistive
        = switch_statement->verilog_switch_resistive;
    if (result->driver_regions.size() != 1U) {
        return result;
    }
    const auto& target = result->driver_regions.front();
    result->switch_target = target.signal;
    const auto source_width = design_.signals_.at(
        *result->switch_source).initial_value.width();
    const auto source_selection = hir_constant_selection(
        *switch_statement->verilog_switch_source,
        semantic::ScopeId { });
    const auto selected_source_width = source_selection
        ? source_selection->width
        : source_width;
    const auto target_width = target.whole
        ? design_.signals_.at(target.signal).initial_value.width()
        : static_cast<std::size_t>(target.width);
    if ((source_selection || !target.whole)
        && selected_source_width == target_width) {
        result->switch_source_offset = source_selection
            ? source_selection->offset
            : 0U;
        result->switch_target_offset = target.whole
            ? 0U
            : target.offset;
        result->switch_width = selected_source_width;
    }
    if (result->switch_bidirectional && result->switch_target) {
        // The runtime's switch network resolves a bidirectional switch from
        // the drivers on both sides. Its directional assignments would
        // become drivers that feed each terminal's value back to itself.
        result->operations.clear();
        result->operations.emplace_back(Halt { });
        result->initialize = false;
    }
    return result;
}

std::optional<std::vector<Lowerer::HirInoutActual>>
Lowerer::hir_inout_actual(const semantic::ExpressionId expression) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto source = specialized_hir_unit_->find_expression(expression);
    if (!source || source->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto kind = source->systemverilog->kind;
    if (kind == semantic::sv::ExpressionKind::concatenation) {
        // The first operand holds the most significant bits.
        std::vector<HirInoutActual> pieces;
        for (const auto operand : source->systemverilog->operands) {
            const auto operand_source
                = specialized_hir_unit_->find_expression(operand);
            std::optional<std::vector<HirInoutActual>> nested;
            if (operand_source && operand_source->systemverilog != nullptr
                && operand_source->systemverilog->kind
                    == semantic::sv::ExpressionKind::name) {
                const auto declaration = hir_target_declaration(operand);
                const auto binding = declaration
                    ? hir_runtime_binding(
                          *declaration, semantic::ScopeId { }, false)
                    : std::nullopt;
                if (!binding || !binding->signal || binding->width == 0U) {
                    return std::nullopt;
                }
                nested = std::vector<HirInoutActual> { HirInoutActual {
                    *binding->signal, 0U, binding->width, 0U } };
            } else {
                nested = hir_inout_actual(operand);
            }
            if (!nested) {
                return std::nullopt;
            }
            pieces.insert(pieces.begin(), nested->begin(), nested->end());
        }
        std::size_t formal_offset { };
        for (auto& piece : pieces) {
            piece.formal_offset += formal_offset;
            formal_offset += piece.width;
        }
        return pieces.empty() ? std::nullopt : std::optional { pieces };
    }
    if (kind != semantic::sv::ExpressionKind::index
        && kind != semantic::sv::ExpressionKind::slice) {
        return std::nullopt;
    }
    const auto declaration = hir_target_declaration(expression);
    const auto binding = declaration
        ? hir_runtime_binding(*declaration, semantic::ScopeId { }, false)
        : std::nullopt;
    const auto selection = hir_constant_selection(
        expression, semantic::ScopeId { });
    if (!binding || !binding->signal || !selection
        || selection->width == 0U) {
        return std::nullopt;
    }
    return std::vector<HirInoutActual> { HirInoutActual {
        *binding->signal, selection->offset, selection->width, 0U } };
}

std::optional<Process> Lowerer::lower_hir_input_actual(
    const semantic::ExpressionId expression,
    const SignalId destination,
    const frontend::Language language,
    const std::string_view hierarchy,
    const std::size_t order,
    std::optional<semantic::vhdl::SubtypeIndication> vhdl_context,
    const std::optional<std::pair<std::uint32_t, std::uint32_t>> actual_slice)
{
    [[maybe_unused]] const HirEffectiveVhdlSubtypeCacheScope cache_scope {
        *this
    };
    if (specialized_hir_unit_ == nullptr
        || destination >= design_.signal_info_.size()) {
        return std::nullopt;
    }
    const auto source = specialized_hir_unit_->find_expression(expression);
    if (!source) {
        return std::nullopt;
    }
    const auto scope = semantic::ScopeId { };
    // Port and primitive actuals are already retained-HIR expressions. Let
    // the direct expression lowerer own their complete surface and precise
    // diagnostics instead of rejecting newer compound forms in the
    // conservative process-capability preflight.
    HirProcessDescription description;
    description.source = source->systemverilog != nullptr
        ? source->systemverilog->source
        : source->vhdl->source;
    description.scope = scope;
    description.name = "port_actual_" + std::to_string(order);
    description.input_actual = expression;
    description.input_actual_destination = destination;
    description.input_actual_vhdl_context = std::move(vhdl_context);
    description.actual_slice = actual_slice;
    description.kind = language == frontend::Language::Vhdl2008
        ? frontend::ProcessKind::VhdlProcess
        : frontend::ProcessKind::SystemVerilogAlwaysComb;
    description.wildcard_sensitivity = true;
    description.require_wildcard_dependency = false;
    description.always_comb_or_latch = true;
    return lower_hir_process_body(description, language, hierarchy);
}

std::optional<Process> Lowerer::lower_hir_package_initializer(
    const semantic::DeclarationId declaration_id,
    const SignalId destination,
    const frontend::Language language,
    const std::string_view hierarchy)
{
    [[maybe_unused]] const HirEffectiveVhdlSubtypeCacheScope cache_scope {
        *this
    };
    if (specialized_hir_unit_ == nullptr
        || language != frontend::Language::SystemVerilog2017
        || destination >= design_.signal_info_.size()) {
        return std::nullopt;
    }
    const auto declaration
        = specialized_hir_unit_->find_declaration(declaration_id);
    if (!declaration || declaration->systemverilog == nullptr
        || declaration->systemverilog->form
            != semantic::sv::DeclarationForm::variable
        || !declaration->systemverilog->initializer
        || !declaration->systemverilog->type
        || !declaration->systemverilog->scope.valid()) {
        return std::nullopt;
    }
    const auto initializer = *declaration->systemverilog->initializer;
    const auto expression
        = specialized_hir_unit_->find_expression(initializer);
    const auto binding = hir_runtime_binding(
        declaration_id, declaration->systemverilog->scope, true);
    if (!expression || expression->systemverilog == nullptr
        || !binding || binding->kind != HirRuntimeBindingKind::signal
        || binding->signal != destination) {
        return std::nullopt;
    }

    HirProcessDescription description;
    description.source = expression->systemverilog->source;
    description.scope = declaration->systemverilog->scope;
    description.name = "$package_static_initializer_"
        + std::to_string(declaration_id.value());
    description.input_actual = initializer;
    description.input_actual_destination = destination;
    description.input_actual_assignment_context = true;
    auto assignment_type = declaration->systemverilog->type;
    if (assignment_type && !assignment_type->target.target.valid()) {
        const auto resolved = hir_resolver().effective_systemverilog_type(
            *assignment_type, declaration->systemverilog->scope);
        if (resolved && resolved->target.target.valid()) {
            assignment_type = *resolved;
        }
    }
    description.input_actual_assignment_type = std::move(assignment_type);
    description.kind = frontend::ProcessKind::Initial;
    description.initial = true;
    description.require_wildcard_dependency = false;
    return lower_hir_process_body(description, language, hierarchy);
}

std::optional<Lowerer::HirVhdlResolverProgram>
Lowerer::lower_hir_vhdl_resolver_process(const SignalId signal,
    const semantic::DeclarationId function, const std::size_t driver_count,
    const std::string_view hierarchy)
{
    [[maybe_unused]] const HirEffectiveVhdlSubtypeCacheScope cache_scope {
        *this
    };
    const auto declaration = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_declaration(function)
        : std::nullopt;
    if (!declaration || declaration->vhdl == nullptr
        || signal >= design_.signal_info_.size()) {
        return std::nullopt;
    }
    HirProcessDescription description;
    description.source = declaration->vhdl->source;
    description.scope = declaration->vhdl->scope;
    const auto& name = design_.signal_info_[signal].name;
    description.name = "resolver_"
        + name.substr(name.find_last_of('.') + 1U);
    description.kind = frontend::ProcessKind::VhdlProcess;
    description.vhdl_resolver = HirProcessDescription::VhdlResolver {
        signal, function, driver_count };
    hir_vhdl_resolver_registers_.reset();
    auto process = lower_hir_process_body(
        description, frontend::Language::Vhdl2008, hierarchy);
    if (!process || !hir_vhdl_resolver_registers_) {
        return std::nullopt;
    }
    // The runtime runs the program once per resolution; it is never
    // scheduled itself.
    process->initialize = false;
    return HirVhdlResolverProgram { std::move(*process),
        hir_vhdl_resolver_registers_->first,
        hir_vhdl_resolver_registers_->second };
}

std::optional<Process> Lowerer::lower_hir_output_actual(
    const SignalId source,
    const semantic::ExpressionId expression,
    const frontend::Language language,
    const std::string_view hierarchy,
    const std::size_t order,
    const std::optional<std::pair<std::uint32_t, std::uint32_t>> actual_slice)
{
    [[maybe_unused]] const HirEffectiveVhdlSubtypeCacheScope cache_scope {
        *this
    };
    if (specialized_hir_unit_ == nullptr
        || source >= design_.signal_info_.size()
        || !specialized_hir_unit_->find_expression(expression)) {
        return std::nullopt;
    }
    HirProcessDescription description;
    const auto target = specialized_hir_unit_->find_expression(expression);
    description.source = target->systemverilog != nullptr
        ? target->systemverilog->source
        : target->vhdl->source;
    description.scope = semantic::ScopeId { };
    description.name = "port_result_" + std::to_string(order);
    description.output_actual_source = source;
    description.output_actual = expression;
    description.actual_slice = actual_slice;
    description.kind = language == frontend::Language::Vhdl2008
        ? frontend::ProcessKind::VhdlProcess
        : frontend::ProcessKind::SystemVerilogAlwaysComb;
    description.wildcard_sensitivity = true;
    description.require_wildcard_dependency = false;
    description.always_comb_or_latch = true;
    return lower_hir_process_body(description, language, hierarchy);
}

bool Lowerer::lower_hir_output_actual_write(
    const semantic::ExpressionId target,
    RegisterId source)
{
    const auto expression = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_expression(target)
        : std::nullopt;
    if (expression && hir_container_element_binding(target)) {
        return lower_hir_packed_copy_out(target, source);
    }
    // An output connected to a concatenation of variables or nets writes
    // each operand its bits, most significant first (23.3.3).
    if (expression && expression->systemverilog != nullptr
        && expression->systemverilog->kind
            == semantic::sv::ExpressionKind::concatenation
        && !expression->systemverilog->operands.empty()) {
        const auto& operands = expression->systemverilog->operands;
        std::vector<std::size_t> widths;
        std::size_t total { };
        for (const auto operand : operands) {
            const auto width = hir_expression_width(
                operand, hir_process_scope_);
            if (!width || *width == 0U
                || *width > std::numeric_limits<std::uint32_t>::max()
                    - total) {
                return false;
            }
            widths.push_back(*width);
            total += *width;
        }
        if (register_width(source) != total) {
            source = resize_register(source, total, false);
        }
        auto offset = total;
        for (std::size_t index { }; index < operands.size(); ++index) {
            offset -= widths[index];
            const auto part = allocate_register(
                widths[index], register_domain(source));
            process_.operations.emplace_back(Extract {
                part,
                source,
                static_cast<std::uint32_t>(offset),
                static_cast<std::uint32_t>(widths[index]),
            });
            if (!lower_hir_output_actual_write(operands[index], part)) {
                return false;
            }
        }
        return true;
    }
    const auto declaration = expression
        ? hir_target_declaration(target)
        : std::nullopt;
    const auto binding = declaration
        ? hir_runtime_binding(*declaration, hir_process_scope_, true)
        : std::nullopt;
    // An output port connected to a member of an interface instance
    // (`.dout(dif.dout)`, IEEE 1800-2017 23.3.3).
    if (expression && !binding && expression->systemverilog != nullptr
        && expression->systemverilog->kind == semantic::sv::ExpressionKind::name
        && expression->systemverilog->text.find('.') != std::string::npos) {
        const auto signal = hir_named_signal(
            expression->systemverilog->text, hir_process_scope_);
        if (!signal || *signal >= design_.signals_.size()) {
            return false;
        }
        const auto width = design_.signals_[*signal].initial_value.width();
        if (register_width(source) != width) {
            source = resize_register(source, width, false);
        }
        process_.operations.emplace_back(WriteUpdate {
            *signal,
            source,
            process_.scheduling_domain == ProcessSchedulingDomain::systemverilog
                ? SignalUpdateDomain::systemverilog_active
                : SignalUpdateDomain::generic,
        });
        return true;
    }
    if (!expression || !binding
        || !signal_binding_is_writable(*binding)) {
        return false;
    }

    std::span<const semantic::ExpressionId> operands;
    std::string_view selection_operation;
    bool index = false;
    bool slice = false;
    if (expression->systemverilog != nullptr) {
        const auto& selected = *expression->systemverilog;
        operands = selected.operands;
        selection_operation = selected.text;
        index = selected.kind == semantic::sv::ExpressionKind::index;
        slice = selected.kind == semantic::sv::ExpressionKind::slice;
    } else {
        const auto& selected = *expression->vhdl;
        operands = selected.operands;
        selection_operation = selected.text;
        index = selected.kind == semantic::vhdl::ExpressionKind::index;
        slice = selected.kind == semantic::vhdl::ExpressionKind::slice;
    }
    const auto selected = index || slice;
    // A select of an element of a multidimensional packed array
    // (`.out(q[0][g])`) names the array through its enclosing selects.
    auto selected_root = selected && !operands.empty()
        ? operands.front()
        : target;
    bool nested_select { };
    for (std::size_t depth { };
        selected && expression->systemverilog != nullptr && depth < 16U;
        ++depth) {
        const auto prefix = specialized_hir_unit_->find_expression(
            selected_root);
        if (!prefix || prefix->systemverilog == nullptr
            || prefix->systemverilog->kind
                != semantic::sv::ExpressionKind::index
            || prefix->systemverilog->operands.size() != 2U
            || prefix->systemverilog->text != "index") {
            break;
        }
        selected_root = prefix->systemverilog->operands.front();
        nested_select = true;
    }
    if (selected
        && (operands.size() != (index ? 2U : 3U)
            || hir_referenced_declaration(selected_root)
                != declaration)) {
        return false;
    }
    if (nested_select
        && !hir_constant_selection(target, hir_process_scope_)) {
        return false;
    }
    const auto member_selection = hir_systemverilog_member_selection(
        selected ? operands.front() : target);
    auto constant_selection = selected
        ? hir_constant_selection(target, hir_process_scope_)
        : std::nullopt;
    if (member_selection) {
        if (member_selection->offset
                > std::numeric_limits<std::uint32_t>::max()
            || member_selection->width
                > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        if (constant_selection) {
            if (member_selection->offset
                > std::numeric_limits<std::size_t>::max()
                    - constant_selection->offset) {
                return false;
            }
            constant_selection->offset += member_selection->offset;
        } else if (!selected) {
            constant_selection = HirConstantSelection {
                member_selection->offset,
                member_selection->width,
            };
        }
    }
    // A constant select partly or wholly outside the declared range writes
    // only its bits inside the range (IEEE 1800-2017 11.5.1).
    if (selected && expression->systemverilog != nullptr && !nested_select
        && !member_selection && !constant_selection) {
        const auto range = hir_expression_range(
            operands.front(), hir_process_scope_);
        const auto first = hir_constant_integer(operands[1]);
        const auto second = slice
            ? hir_constant_integer(operands[2]) : first;
        if (range && first && second) {
            auto low = *first;
            auto high = *first;
            if (slice && selection_operation == "+:") {
                high = *first + *second - 1;
            } else if (slice && selection_operation == "-:") {
                low = *first - *second + 1;
            } else if (slice) {
                low = std::min(*first, *second);
                high = std::max(*first, *second);
            }
            const auto range_low = std::min(range->left, range->right);
            const auto range_high = std::max(range->left, range->right);
            if (high >= low && high - low < 4096
                && (low < range_low || high > range_high)) {
                if (high < range_low || low > range_high) {
                    return true;
                }
                const auto width = static_cast<std::size_t>(high - low + 1);
                if (register_width(source) != width) {
                    source = resize_register(
                        source, width, binding->signed_value);
                }
                const auto clipped_low = std::max(low, range_low);
                const auto clipped_high = std::min(high, range_high);
                const bool descending = range->left >= range->right;
                const auto value_offset = descending
                    ? clipped_low - low : high - clipped_high;
                const auto signal_offset = descending
                    ? clipped_low - range_low : range_high - clipped_high;
                const auto clipped_width = static_cast<std::uint32_t>(
                    clipped_high - clipped_low + 1);
                const auto clipped = allocate_register(
                    clipped_width, register_domain(source));
                process_.operations.emplace_back(Extract { clipped, source,
                    static_cast<std::uint32_t>(value_offset), clipped_width });
                process_.operations.emplace_back(WriteUpdateSlice {
                    *binding->signal,
                    clipped,
                    static_cast<std::uint32_t>(signal_offset),
                    process_.scheduling_domain
                            == ProcessSchedulingDomain::systemverilog
                        ? SignalUpdateDomain::systemverilog_active
                        : SignalUpdateDomain::generic,
                });
                return true;
            }
        }
    }
    const auto dynamic_part_width = slice && !constant_selection
        ? hir_dynamic_part_width(target, hir_process_scope_)
        : std::nullopt;
    const auto write_width = constant_selection
        ? std::optional { constant_selection->width }
        : index ? hir_index_target_width(target)
        : dynamic_part_width ? dynamic_part_width
        : !selected && member_selection
        ? std::optional { member_selection->width }
        : !selected ? std::optional { binding->width }
        : std::nullopt;
    if (!write_width || *write_width == 0U
        || *write_width > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    if (register_width(source) != *write_width) {
        source = resize_register(
            source, *write_width,
            member_selection
                ? member_selection->signed_value
                : binding->signed_value);
    }
    if (!selected && !member_selection
        && binding->domain == frontend::ValueDomain::Integer
        && binding->integer_range) {
        process_.operations.emplace_back(IntegerCheck {
            source,
            std::min(binding->integer_range->left,
                binding->integer_range->right),
            std::max(binding->integer_range->left,
                binding->integer_range->right),
        });
    }
    if (constant_selection) {
        process_.operations.emplace_back(WriteUpdateSlice {
            *binding->signal,
            source,
            static_cast<std::uint32_t>(constant_selection->offset),
            process_.scheduling_domain
                    == ProcessSchedulingDomain::systemverilog
                ? SignalUpdateDomain::systemverilog_active
                : SignalUpdateDomain::generic,
        });
        record_readonly_vhdl_output_write(*binding);
        return true;
    }
    if (!selected) {
        process_.operations.emplace_back(
            WriteUpdate {
                *binding->signal,
                source,
                process_.scheduling_domain
                        == ProcessSchedulingDomain::systemverilog
                    ? SignalUpdateDomain::systemverilog_active
                    : SignalUpdateDomain::generic,
            });
        record_readonly_vhdl_output_write(*binding);
        return true;
    }
    const auto source_width = member_selection
        ? member_selection->width
        : binding->width;
    const auto dynamic = lower_hir_dynamic_index(
        operands[0], operands[1], source_width,
        member_selection
            ? static_cast<std::uint32_t>(member_selection->offset)
            : 0U);
    if (!dynamic) {
        return false;
    }
    if (index && register_width(source) > 1U) {
        // A multidimensional packed array element: the dynamic index holds
        // the element's bit offset.
        process_.operations.emplace_back(WriteUpdateDynamicPartSlice {
            *binding->signal,
            source,
            DynamicPartIndex {
                dynamic->index,
                dynamic->left,
                dynamic->right,
                dynamic->base_offset,
                static_cast<std::uint32_t>(register_width(source)),
                true,
                true,
            },
            process_.scheduling_domain
                    == ProcessSchedulingDomain::systemverilog
                ? SignalUpdateDomain::systemverilog_active
                : SignalUpdateDomain::generic,
        });
        record_readonly_vhdl_output_write(*binding);
        return true;
    }
    if (index) {
        process_.operations.emplace_back(WriteUpdateDynamicSlice {
            *binding->signal,
            source,
            *dynamic,
            process_.scheduling_domain
                    == ProcessSchedulingDomain::systemverilog
                ? SignalUpdateDomain::systemverilog_active
                : SignalUpdateDomain::generic,
        });
        record_readonly_vhdl_output_write(*binding);
        return true;
    }
    if (!dynamic_part_width) {
        return false;
    }
    process_.operations.emplace_back(WriteUpdateDynamicPartSlice {
        *binding->signal,
        source,
        DynamicPartIndex {
            dynamic->index,
            dynamic->left,
            dynamic->right,
            dynamic->base_offset,
            static_cast<std::uint32_t>(*dynamic_part_width),
            selection_operation == "+:",
            dynamic->left >= dynamic->right,
        },
        process_.scheduling_domain
                == ProcessSchedulingDomain::systemverilog
            ? SignalUpdateDomain::systemverilog_active
            : SignalUpdateDomain::generic,
    });
    record_readonly_vhdl_output_write(*binding);
    return true;
}

std::optional<Process> Lowerer::lower_hir_process_body(
    const HirProcessDescription& description,
    const frontend::Language language,
    const std::string_view hierarchy)
{
    // A process whose lowered class function bodies cannot all be lowered
    // is lowered again with those calls left to the runtime interpreter.
    const auto diagnostics_before = diagnostics_.size();
    const auto processes_before = design_.process_count();
    class_functions_lowered_ = false;
    auto lowered = lower_hir_process_body_once(
        description, language, hierarchy);
    // A pass that reported errors failed too, even if it produced a
    // process.
    if ((lowered && diagnostics_.size() == diagnostics_before)
        || !class_functions_lowered_ || !lower_class_functions_
        || design_.process_count() != processes_before) {
        return lowered;
    }
    if (std::getenv("FSIM_HIR_LOWER_TRACE_FAILURES") != nullptr) {
        for (auto index = diagnostics_before; index < diagnostics_.size();
             ++index) {
            std::cerr << "[fsim-hir-lower] class-function retry after "
                      << diagnostics_[index].code << ": "
                      << diagnostics_[index].message << '\n';
        }
    }
    diagnostics_.resize(diagnostics_before);
    lower_class_functions_ = false;
    lowered = lower_hir_process_body_once(description, language, hierarchy);
    lower_class_functions_ = true;
    return lowered;
}

// Clears the register, local and control state of one lowered process.
void Lowerer::reset_hir_process_state()
{
    hir_lowering_rejection_.reset();
    next_register_ = 0U;
    next_string_register_ = 0U;
    next_container_register_ = 0U;
    register_widths_.clear();
    register_domains_.clear();
    locals_.clear();
    hir_local_registers_.clear();
    hir_local_string_registers_.clear();
    hir_local_container_registers_.clear();
    hir_local_container_types_.clear();
    hir_callable_frames_.clear();
    hir_vhdl_captured_scalar_ranges_.clear();
    hir_callable_indices_.clear();
    hir_interface_callable_indices_.clear();
    pending_hir_callables_.clear();
    active_hir_callable_.reset();
    hir_generic_binding_frames_.clear();
    string_locals_.clear();
    debug_local_names_.clear();
    local_scope_.clear();
    loop_controls_.clear();
    block_controls_.clear();
    named_block_controls_.clear();
    named_fork_controls_.clear();
}

std::optional<Process> Lowerer::lower_hir_process_body_once(
    const HirProcessDescription& description,
    const frontend::Language language,
    const std::string_view hierarchy)
{
    [[maybe_unused]] const HirEffectiveVhdlSubtypeCacheScope cache_scope {
        *this
    };
    process_ = Process { };
    generated_processes_.clear();
    implicit_signal_dependencies_.clear();
    deferred_assertion_action_phase_.reset();
    deferred_assertion_action_handoff_.reset();
    language_ = language;
    sample_concurrent_assertion_reads_
        = description.concurrent_assertion;
    hierarchy_ = std::string { hierarchy };
    reset_hir_process_state();
    procedural_continuous_assignments_.clear();
    pending_monitor_drivers_.clear();
    procedural_continuous_assignment_by_statement_.clear();
    procedural_continuous_assignments_by_target_.clear();
    readonly_signal_write_operations_.clear();
    process_.id = static_cast<ProcessId>(design_.process_count());
    hir_process_scope_ = description.scope;
    // Context-typed character literals are classified before any expression
    // view is held; the scan may grow the specialization.
    if (language == frontend::Language::Vhdl2008) {
        collect_hir_vhdl_character_contexts();
    }
    process_.scheduling_domain
        = (language == frontend::Language::SystemVerilog2017
            || language == frontend::Language::Verilog2005)
        ? ProcessSchedulingDomain::systemverilog
        : ProcessSchedulingDomain::generic;
    process_kind_ = description.kind;
    process_.program_owner = systemverilog_program_owner_;
    process_.name = std::string { hierarchy } + "."
        + (description.name.empty()
                ? "process_" + std::to_string(process_.id)
                : description.name);
    process_.final = description.final;
    process_.postponed = description.postponed;
    process_.observed = description.concurrent_assertion;
    if (description.final) {
        process_.initialize = false;
    }

    if (!initialize_hir_declarations(description.declarations)) {
        report(
            "FSIM-ELAB-HIR-001",
            "compiled HIR process '" + process_.name
                + "' failed during local declaration initialization",
            hir_source_span(description.source));
        process_ = { };
        hir_local_registers_.clear();
        hir_local_string_registers_.clear();
        hir_local_container_registers_.clear();
        hir_local_container_types_.clear();
        return std::nullopt;
    }
    process_.static_sensitivity = description.sensitivities;
    // Procedural continuous assignments are also Verilog statements (IEEE
    // 1364-2005 9.3).
    if (language != frontend::Language::Vhdl2008
        && !description.statements.empty()) {
        prepare_hir_procedural_continuous_assignments(
            description.statements);
    }

    std::optional<RegisterId> event_baseline;
    if (description.event_expression) {
        const auto expression_width = hir_expression_width(
            *description.event_expression, hir_process_scope_);
        const auto width = expression_width;
        if (!width || *width == 0U
            || *width > std::numeric_limits<std::uint32_t>::max()) {
            process_ = { };
            return std::nullopt;
        }
        event_baseline = lower_hir_expression(
            *description.event_expression, *width);
        if (!event_baseline) {
            process_ = { };
            return std::nullopt;
        }
    }
    // The least significant bit of an edge-qualified event expression.
    const auto event_bit = [&](const RegisterId value) {
        if (!description.event_expression_scalar_edge
            || register_width(value) == 1U) {
            return value;
        }
        const auto bit = allocate_register(1U, register_domain(value));
        process_.operations.emplace_back(Extract { bit, value, 0U, 1U });
        return bit;
    };
    if (event_baseline) {
        event_baseline = event_bit(*event_baseline);
    }
    if (description.start_after_procedures) {
        process_.startup_phase
            = runtime::simir::ProcessStartupPhase::after_procedures;
    }
    const auto resume_entry = static_cast<InstructionIndex>(
        process_.operations.size());
    std::optional<InstructionIndex> event_filter_branch;
    if (description.event_expression) {
        process_.operations.emplace_back(WaitSensitivity { });
        const auto expression_width = hir_expression_width(
            *description.event_expression, hir_process_scope_);
        auto current = lower_hir_expression(
            *description.event_expression, expression_width.value_or(1U));
        if (!current) {
            process_ = { };
            return std::nullopt;
        }
        current = event_bit(*current);
        const auto zero = allocate_register(
            1U, frontend::ValueDomain::Bit2);
        const auto one = allocate_register(
            1U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            LoadConstant { zero, unsigned_value(0U, 1U) });
        process_.operations.emplace_back(
            LoadConstant { one, unsigned_value(1U, 1U) });
        const auto equal = allocate_register(
            1U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::case_equal,
            equal,
            *event_baseline,
            *current,
        });
        const auto changed = allocate_register(
            1U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LogicalNot { changed, equal });
        auto matched = changed;
        if (description.event_edge != runtime::simir::EdgeKind::any) {
            // A posedge leaves 0 or arrives at 1, and a negedge leaves 1 or
            // arrives at 0 (IEEE 1800-2017 table 9-2); z-to-x is neither.
            const bool rising = description.event_edge
                == runtime::simir::EdgeKind::posedge;
            const auto left_start = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            const auto reached_end = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Binary {
                BinaryOperator::case_equal,
                left_start,
                *event_baseline,
                rising ? zero : one,
            });
            process_.operations.emplace_back(Binary {
                BinaryOperator::case_equal,
                reached_end,
                *current,
                rising ? one : zero,
            });
            const auto edge = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LogicalBinary {
                LogicalBinaryOperator::logical_or,
                edge,
                left_start,
                reached_end,
            });
            const auto edge_matched = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LogicalBinary {
                LogicalBinaryOperator::logical_and,
                edge_matched,
                matched,
                edge,
            });
            matched = edge_matched;
        }
        process_.operations.emplace_back(CopyRegister {
            *event_baseline, *current });
        event_filter_branch = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Branch {
            matched,
            0U,
            resume_entry,
            UnknownBranchPolicy::when_false,
        });
    }
    emit_debug_point(
        DebugPointKind::process_entry,
        hir_source_span(description.source));
    if (event_filter_branch) {
        auto& branch = operation_get<Branch>(
            process_.operations[*event_filter_branch]);
        branch.when_true = static_cast<InstructionIndex>(
            process_.operations.size());
    }
    const auto input_adapter = description.input_actual.has_value()
        || description.input_actual_destination.has_value();
    const auto output_adapter = description.output_actual_source.has_value()
        || description.output_actual.has_value();
    const auto procedural_driver
        = description.procedural_assignment_active.has_value()
        || description.procedural_assignment_target.has_value()
        || description.procedural_assignment_value.has_value();
    if (description.input_actual.has_value()
            != description.input_actual_destination.has_value()
        || (description.input_actual_assignment_context
            && (!description.input_actual
                || !description.input_actual_assignment_type
                || description.actual_slice
                || description.input_actual_vhdl_context
                || language != frontend::Language::SystemVerilog2017))
        || description.output_actual_source.has_value()
            != description.output_actual.has_value()
        || (input_adapter && output_adapter)
        || (procedural_driver
            && (!description.procedural_assignment_active
                || !description.procedural_assignment_target
                || !description.procedural_assignment_value))
        || (procedural_driver && (input_adapter || output_adapter))) {
        process_ = { };
        return std::nullopt;
    }
    if (procedural_driver) {
        // The driver forces its value while its assignment owns the target.
        const auto owner = allocate_register(
            32U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(ReadSignal {
            owner, *description.procedural_assignment_active });
        record_implicit_signal_dependency(
            *description.procedural_assignment_active);
        const auto selected = load_constant(
            unsigned_value(description.procedural_assignment_owner, 32U),
            frontend::ValueDomain::Bit2);
        const auto active = allocate_register(
            1U, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::case_equal, active, owner, selected });
        if (description.procedural_assignment_force_owner) {
            const auto forced = allocate_register(
                32U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(ReadSignal {
                forced, *description.procedural_assignment_force_owner });
            record_implicit_signal_dependency(
                *description.procedural_assignment_force_owner);
            const auto zero = load_constant(
                unsigned_value(0U, 32U), frontend::ValueDomain::Bit2);
            const auto unforced = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Binary {
                BinaryOperator::case_equal, unforced, forced, zero });
            process_.operations.emplace_back(Binary {
                BinaryOperator::bit_and, active, active, unforced });
        }
        const auto branch = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Branch {
            active,
            0U,
            0U,
            UnknownBranchPolicy::when_false,
        });
        const auto drive = static_cast<InstructionIndex>(
            process_.operations.size());
        if (!lower_hir_force_release(
                *description.procedural_assignment_target,
                description.procedural_assignment_value,
                std::nullopt,
                true,
                description.source)) {
            process_ = { };
            return std::nullopt;
        }
        const auto complete = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations[branch] = Branch {
            active,
            drive,
            complete,
            UnknownBranchPolicy::when_false,
        };
    } else if (description.vhdl_resolver) {
        const auto& resolver = *description.vhdl_resolver;
        const auto& signal = design_.signal_info_[resolver.signal];
        if (signal.width == 0U
            || (resolver.driver_count != 0U
                && signal.width > std::numeric_limits<std::size_t>::max()
                    / resolver.driver_count)) {
            process_ = { };
            return std::nullopt;
        }
        // Without connected drivers (a guarded bus) the function receives a
        // null array; the input register keeps one unused element.
        const auto input = allocate_register(
            signal.width * std::max<std::size_t>(resolver.driver_count, 1U),
            signal.source_domain);
        // A composite whose element subtype is resolved resolves each
        // scalar subelement on its own (IEEE 1076-2008 14.7.3.2).
        const auto function_type = hir_callable_type(
            resolver.function, signal.width);
        const auto element_width = function_type
            ? function_type->width
            : std::size_t { };
        const bool per_element = signal.vhdl_array != nullptr
            && element_width != 0U && element_width < signal.width
            && signal.width % element_width == 0U
            && signal.width / element_width <= 4096U;
        const auto output = allocate_register(
            signal.width, signal.source_domain);
        if (per_element) {
            const auto drivers
                = std::max<std::size_t>(resolver.driver_count, 1U);
            const auto elements = signal.width / element_width;
            process_.operations.emplace_back(CopyRegister {
                output,
                load_constant(unsigned_value(0U, signal.width),
                    signal.source_domain) });
            for (std::size_t element { }; element < elements; ++element) {
                const auto offset = element * element_width;
                auto driving = load_constant(
                    unsigned_value(0U, element_width * drivers),
                    signal.source_domain);
                for (std::size_t driver { }; driver < drivers; ++driver) {
                    const auto part = allocate_register(
                        element_width, signal.source_domain);
                    process_.operations.emplace_back(Extract {
                        part, input,
                        static_cast<std::uint32_t>(
                            driver * signal.width + offset),
                        static_cast<std::uint32_t>(element_width) });
                    const auto updated = allocate_register(
                        element_width * drivers, signal.source_domain);
                    process_.operations.emplace_back(Insert {
                        updated, driving, part,
                        static_cast<std::uint32_t>(driver * element_width) });
                    driving = updated;
                }
                const auto resolved = lower_hir_vhdl_resolution_call(
                    resolver.function, driving, resolver.driver_count,
                    element_width);
                if (!resolved || register_width(*resolved) != element_width) {
                    process_ = { };
                    return std::nullopt;
                }
                const auto updated = allocate_register(
                    signal.width, signal.source_domain);
                process_.operations.emplace_back(Insert {
                    updated, output, *resolved,
                    static_cast<std::uint32_t>(offset) });
                process_.operations.emplace_back(
                    CopyRegister { output, updated });
            }
            hir_vhdl_resolver_registers_ = std::pair { input, output };
        } else {
            auto resolved = lower_hir_vhdl_resolution_call(
                resolver.function, input, resolver.driver_count,
                signal.width);
            if (!resolved || register_width(*resolved) != signal.width) {
                process_ = { };
                return std::nullopt;
            }
            process_.operations.emplace_back(
                CopyRegister { output, *resolved });
            hir_vhdl_resolver_registers_ = std::pair { input, output };
        }
    } else if (description.input_actual) {
        const auto destination = *description.input_actual_destination;
        const auto& signal = design_.signal_info_[destination];
        auto actual = *description.input_actual;
        auto actual_expression
            = specialized_hir_unit_->find_expression(actual);
        constexpr auto qualified_prefix
            = semantic::vhdl::qualified_expression_prefix;
        if (description.input_actual_vhdl_context
            && actual_expression
            && actual_expression->vhdl != nullptr
            && actual_expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::call
            && actual_expression->vhdl->text.starts_with(
                qualified_prefix)
            && actual_expression->vhdl->operands.size() == 1U) {
            actual = actual_expression->vhdl->operands.front();
            actual_expression
                = specialized_hir_unit_->find_expression(actual);
        }
        auto value = description.input_actual_vhdl_context
                && actual_expression
                && actual_expression->vhdl != nullptr
                && actual_expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::aggregate
            ? lower_hir_vhdl_aggregate(
                  actual,
                  signal.width,
                  &*description.input_actual_vhdl_context)
            : std::optional<RegisterId> { };
        // One instance of an instance array reads its slice of the actual.
        if (!value && description.actual_slice) {
            const auto natural = hir_expression_width(
                actual, hir_process_scope_);
            const auto [offset, width] = *description.actual_slice;
            const auto whole = natural
                ? lower_hir_expression(actual, *natural) : std::nullopt;
            if (!whole || width != signal.width
                || offset + width > register_width(*whole)) {
                process_ = { };
                return std::nullopt;
            }
            value = allocate_register(width, register_domain(*whole));
            process_.operations.emplace_back(Extract {
                *value, *whole, offset, width });
        }
        bool extend_signed = description.input_actual_assignment_context
            ? hir_expression_signed(actual)
            : signal.is_signed;
        if (description.input_actual_assignment_context && !value) {
            const auto packed_pattern
                = hir_systemverilog_packed_pattern_operand(
                    actual, *description.input_actual_assignment_type);
            if (packed_pattern && *packed_pattern != actual
                && systemverilog_standard_
                    != frontend::StandardRevision::SystemVerilog2023) {
                report(
                    "FSIM-ELAB-SVCAST-005",
                    "assignment-pattern static-cast contextual typing "
                    "requires the exact SystemVerilog-2023 profile",
                    hir_source_span(actual_expression->systemverilog->source));
                process_ = { };
                return std::nullopt;
            }
            if (packed_pattern) {
                value = lower_hir_systemverilog_packed_pattern(
                    *packed_pattern,
                    *description.input_actual_assignment_type,
                    signal.width);
            }
        }
        // Port scalar connections convert real actuals as assignments do
        // (IEEE 1800-2017 23.3.3.7 and 6.12.2).
        if (!description.input_actual_assignment_context && !value
            && actual_expression
            && actual_expression->systemverilog != nullptr
            && frontend::is_real_scalar_kind(signal.systemverilog_scalar)
                != frontend::is_real_scalar_kind(
                    hir_systemverilog_scalar_kind(actual))) {
            value = lower_hir_scalar_converted_value(
                actual, signal.width, signal.systemverilog_scalar);
            extend_signed = true;
        }
        // An unsized fill literal (`'1`) fills the port's width (IEEE
        // 1800-2017 5.7.1).
        const auto fill_literal = [](const std::string_view text) {
            return text == "'0" || text == "'1" || text == "'x"
                || text == "'X" || text == "'z" || text == "'Z";
        };
        // A port connection extends a narrower actual by the actual's own
        // signedness, as a continuous assignment does (IEEE 1800-2017
        // 23.3.3.7 and 10.7).
        if (!description.input_actual_assignment_context && !value
            && actual_expression
            && actual_expression->systemverilog != nullptr
            && !fill_literal(actual_expression->systemverilog->text)) {
            const auto natural = hir_expression_width(
                actual, hir_process_scope_);
            if (natural && *natural != 0U && *natural < signal.width) {
                value = lower_hir_expression(actual, *natural);
                extend_signed = hir_expression_signed(actual);
            }
        }
        if (!value) {
            value = description.input_actual_assignment_context
                ? lower_hir_scalar_converted_value(
                      actual, signal.width, signal.systemverilog_scalar)
                : lower_hir_expression(actual, signal.width);
        }
        if (!value) {
            process_ = { };
            return std::nullopt;
        }
        if (register_width(*value) != signal.width) {
            value = resize_register(
                *value, signal.width, extend_signed);
        }
        if (register_domain(*value) != signal.source_domain) {
            const auto converted = allocate_register(
                signal.width, signal.source_domain);
            process_.operations.emplace_back(CopyRegister {
                converted, *value });
            value = converted;
        }
        if (signal.integer_range
            && (signal.source_domain == frontend::ValueDomain::Integer
                || (signal.source_domain == frontend::ValueDomain::Bit2
                    && signal.enumeration_range))) {
            process_.operations.emplace_back(IntegerCheck {
                *value,
                std::min(signal.integer_range->left,
                    signal.integer_range->right),
                std::max(signal.integer_range->left,
                    signal.integer_range->right),
            });
        }
        if (description.input_actual_assignment_context) {
            // Package declaration initializers are ordered startup actions:
            // publish the completed assignment into the startup baseline.
            process_.operations.emplace_back(WriteBlocking {
                destination, *value });
        } else {
            process_.operations.emplace_back(WriteUpdate {
                destination,
                *value,
                process_.scheduling_domain
                    == ProcessSchedulingDomain::systemverilog
                    ? SignalUpdateDomain::systemverilog_active
                    : SignalUpdateDomain::generic,
            });
        }
        record_readonly_signal_write(destination);
    } else if (description.output_actual) {
        const auto source = *description.output_actual_source;
        const auto& signal = design_.signal_info_[source];
        auto value = allocate_register(
            signal.width, signal.source_domain);
        process_.operations.emplace_back(ReadSignal { value, source });
        record_implicit_signal_dependency(source);
        // A real output port converts to an integral actual, and an
        // integral one to a real actual (23.3.3.7, 6.12.2).
        if (const auto actual_kind
            = hir_systemverilog_scalar_kind(*description.output_actual);
            !description.actual_slice
            && specialized_hir_unit_->find_expression(
                   *description.output_actual)
                   ->systemverilog
                != nullptr
            && frontend::is_real_scalar_kind(signal.systemverilog_scalar)
                != frontend::is_real_scalar_kind(actual_kind)) {
            value = frontend::is_real_scalar_kind(actual_kind)
                ? convert_hir_integral_to_real(
                      value, signal.is_signed, actual_kind)
                : convert_hir_real_to_integral(
                      value, signal.systemverilog_scalar);
        }
        // A signed output port connected to a wider actual extends by its
        // own signedness, as a continuous assignment does (23.3.3.7).
        if (const auto actual_width = hir_expression_width(
                *description.output_actual, hir_process_scope_);
            !description.actual_slice && signal.is_signed && actual_width
            && *actual_width > register_width(value)
            && !frontend::is_real_scalar_kind(signal.systemverilog_scalar)) {
            value = resize_register(value, *actual_width, true);
        }
        if (description.actual_slice) {
            // One instance of an instance array drives its slice of the
            // actual net.
            auto target = hir_direct_signal(*description.output_actual);
            std::size_t actual_offset { };
            std::size_t actual_width = target
                ? design_.signal_info_[*target].width
                : 0U;
            // A constant bit- or part-select actual (`.o(bus[17:10])`)
            // places the slice within its signal.
            if (const auto selected = specialized_hir_unit_->find_expression(
                    *description.output_actual);
                !target && selected && selected->systemverilog != nullptr
                && (selected->systemverilog->kind
                        == semantic::sv::ExpressionKind::index
                    || selected->systemverilog->kind
                        == semantic::sv::ExpressionKind::slice)
                && !selected->systemverilog->operands.empty()) {
                const auto root = hir_direct_signal(
                    selected->systemverilog->operands.front());
                const auto selection = hir_constant_selection(
                    *description.output_actual, hir_process_scope_);
                if (root && selection) {
                    target = root;
                    actual_offset = selection->offset;
                    actual_width = selection->width;
                }
            }
            const auto [offset, width] = *description.actual_slice;
            if (!target || width != signal.width
                || offset + width > actual_width
                || actual_offset + actual_width
                    > design_.signal_info_[*target].width) {
                process_ = { };
                return std::nullopt;
            }
            process_.operations.emplace_back(WriteUpdateSlice {
                *target,
                value,
                static_cast<std::uint32_t>(actual_offset + offset),
                process_.scheduling_domain
                        == ProcessSchedulingDomain::systemverilog
                    ? SignalUpdateDomain::systemverilog_active
                    : SignalUpdateDomain::generic,
            });
        } else if (const auto subelements = specialized_hir_unit_->find_expression(
                       *description.output_actual);
            subelements && subelements->vhdl != nullptr
            && subelements->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate
            && subelements->vhdl->text == "@vhdl-formal-subelements") {
            // An output formal associated slice by slice (IEEE 1076-2008
            // 6.5.7.1): each actual receives its part of the formal.
            if (!signal.packed_range
                || signal.packed_range->width() != register_width(value)) {
                process_ = { };
                return std::nullopt;
            }
            const auto& range = *signal.packed_range;
            const bool descending = range.left >= range.right;
            const auto low = std::min(range.left, range.right);
            const auto high = std::max(range.left, range.right);
            const auto position = [&](const std::int64_t index)
                -> std::optional<std::uint32_t> {
                if (index < low || index > high) {
                    return std::nullopt;
                }
                return static_cast<std::uint32_t>(descending
                        ? index - range.right
                        : range.right - index);
            };
            for (const auto& association : subelements->vhdl->associations) {
                if (association.choices.size() != 1U) {
                    process_ = { };
                    return std::nullopt;
                }
                const auto choice = specialized_hir_unit_->find_expression(
                    association.choices.front());
                std::optional<std::int64_t> first;
                std::optional<std::int64_t> last;
                if (choice && choice->vhdl != nullptr
                    && choice->vhdl->kind == semantic::vhdl::ExpressionKind::binary
                    && (choice->vhdl->text == "to" || choice->vhdl->text == "downto")
                    && choice->vhdl->operands.size() == 2U) {
                    first = hir_constant_integer(choice->vhdl->operands[0]);
                    last = hir_constant_integer(choice->vhdl->operands[1]);
                } else {
                    first = hir_constant_integer(association.choices.front());
                    last = first;
                }
                const auto first_position = first ? position(*first) : std::nullopt;
                const auto last_position = last ? position(*last) : std::nullopt;
                if (!first_position || !last_position) {
                    process_ = { };
                    return std::nullopt;
                }
                const auto offset = std::min(*first_position, *last_position);
                const auto width = std::max(*first_position, *last_position)
                    - offset + 1U;
                const auto part = allocate_register(width, register_domain(value));
                process_.operations.emplace_back(Extract {
                    part, value, offset, width });
                if (!lower_hir_output_actual_write(association.value, part)) {
                    process_ = { };
                    return std::nullopt;
                }
            }
        } else if (!lower_hir_output_actual_write(
                *description.output_actual, value)) {
            process_ = { };
            return std::nullopt;
        }
    } else if (description.vhdl_guarded_assignment) {
        // Without a guard expression, a guarded assignment is controlled by
        // a visible signal named GUARD (IEEE 1076-2008 11.6).
        std::optional<SignalId> guard_signal;
        if (!description.vhdl_guard && !description.statements.empty()) {
            const auto statement = specialized_hir_unit_->find_statement(
                description.statements.front());
            if (statement && statement->vhdl != nullptr) {
                semantic::vhdl::Name reference;
                reference.spelling = "guard";
                reference.canonical = "guard";
                const semantic::CompiledDeclarationPredicate signal_object
                    = [](const semantic::CompiledDeclarationView& candidate) {
                          return candidate.vhdl != nullptr
                              && (candidate.vhdl->form
                                      == semantic::vhdl::DeclarationForm::signal
                                  || candidate.vhdl->form
                                      == semantic::vhdl::DeclarationForm::port);
                      };
                const auto declaration
                    = hir_resolver()
                          .resolve_vhdl(
                              reference, statement->vhdl->scope, signal_object)
                          .unique();
                const auto binding = declaration
                    ? hir_runtime_binding(
                          *declaration, hir_process_scope_, false)
                    : std::nullopt;
                if (binding && binding->signal && binding->width == 1U) {
                    guard_signal = binding->signal;
                }
            }
        }
        if (!description.vhdl_guard && !guard_signal) {
            report(
                "FSIM-ELAB-VHDLGUARD-001",
                "a guarded concurrent assignment requires an enclosing "
                "Boolean-guarded block",
                hir_source_span(description.source));
        } else {
            std::optional<RegisterId> guard;
            if (guard_signal) {
                guard = allocate_register(1U, frontend::ValueDomain::Logic4);
                process_.operations.emplace_back(ReadSignal {
                    *guard, *guard_signal });
            } else {
                const auto guard_width = hir_expression_width(
                    *description.vhdl_guard, hir_process_scope_)
                                             .value_or(1U);
                guard = lower_hir_expression(
                    *description.vhdl_guard, guard_width);
            }
            if (guard && register_width(*guard) != 1U) {
                const auto zero = allocate_register(
                    1U, frontend::ValueDomain::Logic4);
                process_.operations.emplace_back(LogicalNot {
                    zero, *guard });
                const auto truth = allocate_register(
                    1U, frontend::ValueDomain::Logic4);
                process_.operations.emplace_back(LogicalNot {
                    truth, zero });
                guard = truth;
            }
            if (!guard) {
                process_ = { };
                return std::nullopt;
            }
            const auto branch = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Branch {
                *guard, 0U, 0U, UnknownBranchPolicy::error });
            const auto active = static_cast<InstructionIndex>(
                process_.operations.size());
            if (!lower_hir_statements(description.statements)) {
                process_ = { };
                return std::nullopt;
            }
            const auto finish = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Jump { 0U });
            const auto inactive = static_cast<InstructionIndex>(
                process_.operations.size());
            const auto assignment_leaf = [&](const auto& self,
                                             const semantic::StatementId id)
                -> std::optional<semantic::ExpressionId> {
                const auto statement
                    = specialized_hir_unit_->find_statement(id);
                if (!statement || statement->vhdl == nullptr) {
                    return std::nullopt;
                }
                if (statement->vhdl->kind
                        == semantic::vhdl::StatementKind::signal_assignment
                    && statement->vhdl->target) {
                    return statement->vhdl->target;
                }
                for (const auto child : statement->vhdl->statements) {
                    if (const auto found = self(self, child)) {
                        return found;
                    }
                }
                for (const auto child : statement->vhdl->else_statements) {
                    if (const auto found = self(self, child)) {
                        return found;
                    }
                }
                for (const auto& alternative :
                    statement->vhdl->alternatives) {
                    for (const auto child : alternative.statements) {
                        if (const auto found = self(self, child)) {
                            return found;
                        }
                    }
                }
                return std::nullopt;
            };
            std::optional<semantic::ExpressionId> target;
            for (const auto statement : description.statements) {
                target = assignment_leaf(assignment_leaf, statement);
                if (target) {
                    break;
                }
            }
            const auto declaration = target
                ? hir_target_declaration(*target)
                : std::nullopt;
            const auto binding = declaration
                ? hir_runtime_binding(
                      *declaration, hir_process_scope_, true)
                : std::nullopt;
            if (!binding || !binding->signal) {
                process_ = { };
                return std::nullopt;
            }
            const auto target_record
                = specialized_hir_unit_->find_declaration(*declaration);
            const auto guarded_kind = target_record
                    && target_record->vhdl != nullptr
                ? target_record->vhdl->guarded_kind
                : std::uint8_t { };
            if (binding->domain != frontend::ValueDomain::Logic9
                && guarded_kind == 0U) {
                // The target is not a guarded signal: a false guard leaves
                // its driver unchanged (IEEE 1076-2008 11.6).
            } else if (binding->domain != frontend::ValueDomain::Logic9) {
                // A guarded signal's driver is disconnected by a null
                // transaction after the disconnection delay (6.9).
                const auto delay = [&]()
                    -> std::optional<runtime::SimulationTick> {
                    if (!description.vhdl_disconnection_delay) {
                        return runtime::SimulationTick { };
                    }
                    const auto& source
                        = description.vhdl_disconnection_delay->primary;
                    if (!source.expression) {
                        return source.magnitude;
                    }
                    const auto evaluated = specialized_hir_unit_
                        ->evaluate_integral_expression(*source.expression);
                    if (!evaluated || *evaluated < 0
                        || (*evaluated != 0
                            && source.magnitude
                                > std::numeric_limits<std::uint64_t>::max()
                                    / static_cast<std::uint64_t>(
                                        *evaluated))) {
                        return std::nullopt;
                    }
                    return source.magnitude
                        * static_cast<std::uint64_t>(*evaluated);
                }();
                if (!delay) {
                    process_ = { };
                    return std::nullopt;
                }
                const auto ignored = allocate_register(
                    1U, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(ClassStaticMethodCall {
                    ignored,
                    "@vhdl-disconnect:" + std::to_string(*binding->signal)
                        + ":" + std::to_string(*delay),
                    { }, { }, { }, 1U, { } });
                record_readonly_vhdl_output_write(*binding);
            } else {
                const auto delay = [&]()
                    -> std::optional<runtime::SimulationTick> {
                    if (!description.vhdl_disconnection_delay) {
                        return runtime::SimulationTick { };
                    }
                    const auto& source
                        = description.vhdl_disconnection_delay->primary;
                    auto magnitude = source.magnitude;
                    if (!source.expression) {
                        return magnitude;
                    }
                    const auto evaluated = specialized_hir_unit_
                        ->evaluate_integral_expression(*source.expression);
                    if (!evaluated || *evaluated < 0) {
                        return std::nullopt;
                    }
                    const auto multiplier
                        = static_cast<std::uint64_t>(*evaluated);
                    if (multiplier != 0U
                        && magnitude
                            > std::numeric_limits<std::uint64_t>::max()
                                / multiplier) {
                        return std::nullopt;
                    }
                    return magnitude * multiplier;
                }();
                if (!delay) {
                    process_ = { };
                    return std::nullopt;
                }
                const auto disconnected = allocate_register(
                    binding->width, frontend::ValueDomain::Logic9);
                auto value = PackedLogic4(binding->width, Logic4::z);
                value.fill(runtime::Logic9::z);
                process_.operations.emplace_back(LoadConstant {
                    disconnected, std::move(value) });
                process_.operations.emplace_back(WriteProjected {
                    *binding->signal,
                    disconnected,
                    *delay,
                    *delay,
                    ProjectedDelayMode::inertial,
                });
                record_readonly_vhdl_output_write(*binding);
            }
            const auto end = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations[branch] = Branch {
                *guard, active, inactive, UnknownBranchPolicy::error };
            process_.operations[finish] = Jump { end };
        }
    } else if (!lower_hir_statements(description.statements)) {
        process_ = { };
        hir_local_registers_.clear();
        hir_local_string_registers_.clear();
        hir_local_container_registers_.clear();
        hir_local_container_types_.clear();
        return std::nullopt;
    }
    auto callable_body_begin = process_.operations.size();
    if (!lower_pending_hir_callables()) {
        report(
            "FSIM-ELAB-HIR-001",
            "compiled HIR process '" + process_.name
                + "' failed while lowering a deferred callable body",
            hir_source_span(description.source));
        process_ = { };
        hir_local_registers_.clear();
        hir_callable_frames_.clear();
        hir_callable_indices_.clear();
        hir_interface_callable_indices_.clear();
        pending_hir_callables_.clear();
        active_hir_callable_.reset();
        hir_generic_binding_frames_.clear();
        return std::nullopt;
    }
    const auto callable_suspends = std::ranges::any_of(
        std::span { process_.operations }.subspan(callable_body_begin),
        [](const Operation& operation) {
            return operation_holds<WaitFor>(operation)
                || operation_holds<WaitOn>(operation)
                || operation_holds<WaitOrder>(operation)
                || operation_holds<WaitForever>(operation)
                || operation_holds<WaitFork>(operation)
                || operation_holds<ProcessAwait>(operation);
        });
    if (language == frontend::Language::Vhdl2008) {
        std::vector<bool> suspends(hir_callable_frames_.size());
        std::vector<std::vector<std::size_t>> dependencies(
            hir_callable_frames_.size());
        for (std::size_t frame_index { };
            frame_index < hir_callable_frames_.size(); ++frame_index) {
            const auto target = hir_callable_frames_[frame_index].target;
            if (target && *target < process_.operations.size()) {
                for (auto operation_index = *target;
                    operation_index < process_.operations.size();
                    ++operation_index) {
                    const auto& operation
                        = process_.operations[operation_index];
                    if (operation_holds<WaitFor>(operation)
                        || operation_holds<WaitOn>(operation)
                        || operation_holds<WaitForever>(operation)) {
                        suspends[frame_index] = true;
                    }
                    if (const auto* call
                        = operation_get_if<Call>(&operation)) {
                        const auto nested = std::ranges::find_if(
                            hir_callable_frames_,
                            [&](const HirCallableFrame& candidate) {
                                return candidate.target
                                    && *candidate.target == call->target;
                            });
                        if (nested != hir_callable_frames_.end()) {
                            dependencies[frame_index].push_back(
                                static_cast<std::size_t>(std::distance(
                                    hir_callable_frames_.begin(), nested)));
                        }
                    }
                    if (operation_holds<Return>(operation)) {
                        break;
                    }
                }
            }
        }
        const auto directly_suspends = suspends;
        bool changed { true };
        while (changed) {
            changed = false;
            for (std::size_t frame_index { };
                frame_index < dependencies.size(); ++frame_index) {
                if (!suspends[frame_index]
                    && std::ranges::any_of(
                        dependencies[frame_index],
                        [&](const std::size_t dependency) {
                            return suspends[dependency];
                        })) {
                    suspends[frame_index] = true;
                    changed = true;
                }
            }
        }
        std::unordered_set<std::uint32_t> reported_functions;
        for (std::size_t frame_index { };
            frame_index < hir_callable_frames_.size(); ++frame_index) {
            const auto& frame = hir_callable_frames_[frame_index];
            if (!frame.function || !suspends[frame_index]
                || !reported_functions.insert(
                    frame.declaration.value()).second) {
                continue;
            }
            const auto declaration
                = specialized_hir_unit_->find_declaration(
                    frame.declaration);
            if (!declaration || declaration->vhdl == nullptr) {
                continue;
            }
            report(
                "FSIM-ELAB-VHLEGAL-009",
                "VHDL function '" + declaration->vhdl->name
                    + (directly_suspends[frame_index]
                            ? "' contains a wait statement"
                            : "' calls a suspending procedure"),
                hir_source_span(declaration->vhdl->source));
        }
    }
    if (language != frontend::Language::Vhdl2008
        && (description.final || description.always_comb_or_latch)
        && callable_suspends) {
        report(
            "FSIM-ELAB-SVTASK-010",
            "a task called from final, always_comb, or always_latch may not "
            "suspend",
            hir_source_span(description.source));
    }
    bool wildcard_never_runs { };
    if (description.wildcard_sensitivity) {
        for (const auto dependency : implicit_signal_dependencies_) {
            process_.static_sensitivity.push_back(dependency);
        }
        if (description.require_wildcard_dependency
            && !description.always_comb_or_latch
            && process_.static_sensitivity.empty()) {
            // `@*` with no readable signals waits for an event that never
            // occurs (IEEE 1800-2017 9.4.2.2), so the process never runs.
            wildcard_never_runs = true;
        }
    }
    runtime::simir::normalize_sensitivities(process_.static_sensitivity);
    std::ranges::sort(
        process_.static_sensitivity,
        {},
        [&](const runtime::simir::Sensitivity& sensitivity) {
            const auto name = sensitivity.signal
                    < design_.signal_info_.size()
                ? std::string_view {
                      design_.signal_info_[sensitivity.signal].name }
                : std::string_view { };
            return std::tuple {
                name,
                static_cast<std::uint8_t>(sensitivity.edge),
                sensitivity.offset,
                sensitivity.width,
            };
        });
    auto duplicate = std::ranges::unique(process_.static_sensitivity);
    process_.static_sensitivity.erase(duplicate.begin(), duplicate.end());
    const auto shift_readonly_output_writes
        = [&](const std::size_t position, const std::size_t count) {
              if (count == 0U) {
                  return;
              }
              std::map<std::size_t, SignalId> shifted;
              for (const auto& [index, signal] :
                   readonly_signal_write_operations_) {
                  shifted.emplace(
                      index >= position ? index + count : index, signal);
              }
              readonly_signal_write_operations_.swap(shifted);
          };
    const auto waits_before_first_execution
        = description.wait_before_first_execution
        || (language != frontend::Language::Vhdl2008
            && !description.initial && !description.always_comb_or_latch
            && !process_.static_sensitivity.empty());
    if ((waits_before_first_execution || wildcard_never_runs)
        && !description.event_expression) {
        for (auto& operation : process_.operations) {
            relocate_instruction_targets(operation, resume_entry);
        }
        shift_readonly_output_writes(resume_entry, 1U);
        if (wildcard_never_runs) {
            process_.operations.insert(
                process_.operations.cbegin() + resume_entry,
                WaitForever { });
        } else {
            process_.operations.insert(
                process_.operations.cbegin() + resume_entry,
                WaitSensitivity { });
        }
        ++callable_body_begin;
    }

    const auto executes_once = description.always_comb_or_latch
        && process_.static_sensitivity.empty();
    std::optional<InstructionIndex> vhdl_trailing_wait;
    std::vector<Operation> process_termination;
    if (description.initial || description.final || executes_once
        || description.vhdl_resolver) {
        process_termination.emplace_back(Halt { });
    } else if (waits_before_first_execution
        || description.body_timed_always) {
        process_termination.emplace_back(Jump { resume_entry });
    } else if (language == frontend::Language::Vhdl2008
        && process_.static_sensitivity.empty()) {
        // Keep the implicit trailing suspension as a placeholder until the
        // called HIR bodies have been lowered. A direct or transitively called
        // VHDL wait makes the process self-suspending and replaces this point
        // with its process-repeat jump below.
        vhdl_trailing_wait = static_cast<InstructionIndex>(
            callable_body_begin);
        process_termination.emplace_back(WaitSensitivity { });
        process_termination.emplace_back(Jump { resume_entry });
    } else {
        process_termination.emplace_back(WaitSensitivity { });
        process_termination.emplace_back(Jump { resume_entry });
    }
    for (std::size_t inserted { };
        inserted < process_termination.size(); ++inserted) {
        for (std::size_t index { };
            index < process_.operations.size(); ++index) {
            auto& operation = process_.operations[index];
            if (index < callable_body_begin) {
                auto* call = operation_get_if<Call>(&operation);
                if (call != nullptr
                    && call->target >= callable_body_begin) {
                    ++call->target;
                }
                continue;
            }
            relocate_instruction_targets(
                operation,
                static_cast<InstructionIndex>(callable_body_begin));
        }
    }
    const auto termination_count = process_termination.size();
    shift_readonly_output_writes(callable_body_begin, termination_count);
    process_.operations.insert(
        process_.operations.cbegin()
            + static_cast<std::ptrdiff_t>(callable_body_begin),
        std::make_move_iterator(process_termination.begin()),
        std::make_move_iterator(process_termination.end()));
    const auto vhdl_explicit_wait
        = language == frontend::Language::Vhdl2008
        && std::ranges::any_of(
            process_.operations,
            [](const Operation& operation) {
                return operation_holds<WaitFor>(operation)
                    || operation_holds<WaitOn>(operation)
                    || operation_holds<WaitForever>(operation);
            });
    if (vhdl_trailing_wait && vhdl_explicit_wait) {
        process_.operations[*vhdl_trailing_wait] = Jump { resume_entry };
    }
    // A concurrent procedure call is a process that calls the procedure and
    // then waits on its signal actuals (IEEE 1076-2008 11.4); the
    // procedure may itself wait.
    const auto concurrent_procedure_call = [&] {
        if (description.statements.size() != 1U
            || specialized_hir_unit_ == nullptr) {
            return false;
        }
        const auto call = specialized_hir_unit_->find_statement(
            description.statements.front());
        return call && call->vhdl != nullptr
            && call->vhdl->kind
                == semantic::vhdl::StatementKind::procedure_call
            && call->vhdl->source == description.source;
    }();
    if (!process_.static_sensitivity.empty() && vhdl_explicit_wait
        && !concurrent_procedure_call) {
        report(
            "FSIM-VHDL-SEM-012",
            "a process sensitivity list cannot be combined with a direct or "
            "transitively called wait statement",
            hir_source_span(description.source));
    }
    validate_read_only_signal_writes(hir_source_span(description.source));
    process_.register_count = next_register_;
    process_.string_register_count = next_string_register_;
    process_.container_register_count = next_container_register_;
    process_.register_value_kinds.reserve(register_domains_.size());
    for (const auto domain : register_domains_) {
        process_.register_value_kinds.push_back(value_kind(domain));
    }
    process_.driver_regions = collect_driver_regions(
        process_, register_widths_);
    materialize_hir_procedural_continuous_assignments();
    materialize_hir_monitor_drivers();

    reset_hir_process_state();
    return std::move(process_);
}

} // namespace fsim::elaboration
