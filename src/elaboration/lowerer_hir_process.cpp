// SPDX-License-Identifier: Apache-2.0
#include "lowerer_driver_regions.hpp"
#include "lowerer_internal.hpp"

#include <array>

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
    const std::unordered_map<std::string, SignalId>& signals,
    const std::unordered_set<SignalId>& read_only_signals,
    const std::unordered_map<std::string, StringObjectId>& string_objects,
    const std::unordered_set<StringObjectId>& read_only_string_objects,
    const std::unordered_map<std::string, ContainerObjectId>&
        container_objects,
    const std::unordered_set<std::string>& read_only_container_objects,
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

void Lowerer::report(
    std::string code,
    std::string message,
    frontend::SourceSpan span)
{
    diagnostics_.push_back({
        std::move(code), std::move(message), std::move(span) });
}

void Lowerer::validate_read_only_signal_writes(
    const frontend::SourceSpan& source,
    const std::optional<SignalId> permitted_signal)
{
    std::set<SignalId> reported;
    const auto check = [&](const SignalId signal) {
        if (read_only_signals_.contains(signal)
            && (!permitted_signal || signal != *permitted_signal)
            && reported.insert(signal).second) {
            report(
                "FSIM-ELAB-SVIFACE-006",
                "an input signal is read-only within process '"
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
                    || std::is_same_v<
                        Operation, WriteProjectedWaveformSlice>
                    || std::is_same_v<Operation, ForceSignalSlice>) {
                    check(candidate.signal);
                }
            },
            operation);
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

void Lowerer::emit_debug_point(
    const DebugPointKind kind,
    const frontend::SourceSpan& span)
{
    process_.operations.emplace_back(DebugPoint {
        kind,
        SourceLocation {
            span.source_name.str(),
            static_cast<std::uint32_t>(span.begin.line),
            static_cast<std::uint32_t>(span.begin.column),
        },
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
        const auto record = specialized_hir_unit_ != nullptr
            ? specialized_hir_unit_->find_expression(current)
            : std::nullopt;
        if (!record) {
            return false;
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
        auto result = frame.debug_name.empty()
            ? "@hir-callable-"
                + std::to_string(frame.declaration.value()) + "::"
                + std::string { name }
            : frame.debug_name + "." + std::string { name };
        if (!debug_local_names_.emplace(result).second) {
            result += "@hir-callable-"
                + std::to_string(frame.invocation_identity);
            debug_local_names_.emplace(result);
        }
        return result;
    };
    std::unordered_set<std::string> declared_here;
    for (const auto declaration_id : declarations) {
        const auto declaration = specialized_hir_unit_->find_declaration(
            declaration_id);
        if (!declaration) {
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
            if (declaration->vhdl->form != Form::variable
                && declaration->vhdl->form != Form::file) {
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
        if (declaration->systemverilog != nullptr
            && declaration->systemverilog->type
            && declaration->systemverilog->type->container_form) {
            const auto& input = *declaration->systemverilog;
            const auto type = hir_systemverilog_container_type(*input.type);
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
                = active_hir_callable_
                && !hir_callable_frames_[*active_hir_callable_]
                        .type.automatic
                && !first_initialization;
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
                        SourceLocation {
                            span.source_name.str(),
                            static_cast<std::uint32_t>(span.begin.line),
                            static_cast<std::uint32_t>(span.begin.column),
                        },
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
                const auto initial = pattern
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
                        SourceLocation {
                            span.source_name.str(),
                            static_cast<std::uint32_t>(span.begin.line),
                            static_cast<std::uint32_t>(span.begin.column),
                        },
                    });
            }
            const bool retained_static
                = active_hir_callable_
                && !hir_callable_frames_[*active_hir_callable_]
                        .type.automatic
                && !first_initialization;
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
                = active_hir_callable_
                && !hir_callable_frames_[*active_hir_callable_]
                        .type.automatic
                && !first_initialization;
            if (first_initialization) {
                hir_local_string_registers_.emplace(
                    declaration_id.value(), destination);
            }
            string_locals_.insert_or_assign(
                string_binding->name, destination);
            if (!retained_static) {
                process_.operations.emplace_back(LoadStringConstant {
                    destination, { } });
            }
            if (initializer && !retained_static) {
                const auto initial = lower_hir_string_expression(
                    *initializer);
                if (!initial) {
                    return false;
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
                    SourceLocation {
                        span.source_name.str(),
                        static_cast<std::uint32_t>(span.begin.line),
                        static_cast<std::uint32_t>(span.begin.column),
                    },
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
                if (const auto separator = type_name.find_last_of(".:");
                    separator != std::string_view::npos) {
                    type_name.remove_prefix(separator + 1U);
                }
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
                })) {
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
            = active_hir_callable_
            && !hir_callable_frames_[*active_hir_callable_]
                    .type.automatic
            && !first_initialization;
        locals_.insert_or_assign(binding->name, destination);
        if (first_initialization) {
            hir_local_registers_.emplace(
                declaration_id.value(), destination);
        }
        if (!retained_static && binding->width != 0U) {
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
                : lower_hir_expression(*initializer, binding->width);
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
        const bool first_debug_name = active_hir_callable_
            || debug_local_names_.emplace(name).second;
        if (first_initialization && !loop_control && first_debug_name) {
            process_.debug_locals.push_back(DebugLocal {
                std::move(name),
                std::move(type_name),
                destination,
                binding->width,
                SourceLocation {
                    span.source_name.str(),
                    static_cast<std::uint32_t>(span.begin.line),
                    static_cast<std::uint32_t>(span.begin.column),
                },
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
        description.concurrent_assertion = input.concurrent_assertion;
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
                    if (!width || *width == 0U || *width > 64U
                        || (sensitivity.edge
                                != semantic::sv::EdgeKind::any
                            && *width != 1U)) {
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
                const auto found = signals_.find(sensitivity.signal);
                if (found != signals_.end()) {
                    signal = found->second;
                }
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
                const auto found = signals_.find(sensitivity.signal);
                if (found != signals_.end()) {
                    signal = found->second;
                }
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

bool Lowerer::can_lower_hir_concurrent_statement(
    const semantic::StatementId statement) const
{
    const auto source = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_statement(statement)
        : std::nullopt;
    if (!source) {
        return false;
    }
    // A concurrent statement is not a lexical process scope. Its HIR
    // expressions retain the enclosing unit scope so name lookup still finds
    // module variables and architecture signals, but those declarations own
    // DesignIR signal storage rather than process-local registers. Use the
    // invalid scope sentinel for the local-storage classification performed by
    // the shared statement capability walker.
    const auto scope = semantic::ScopeId { };
    std::unordered_set<std::uint32_t> visiting;
    if (can_lower_hir_statement(statement, scope, visiting)) {
        return true;
    }

    // Continuous-assignment diagnostics, callable result sizing, and compound
    // lvalue/value handling belong to the direct statement lowerer. The
    // shared capability walker is intentionally conservative about selected
    // package types and packed member expressions, so do not replace a
    // source-preserving lowering attempt with the generic adapter diagnostic.
    if (source->vhdl != nullptr) {
        const auto& input = *source->vhdl;
        if (input.kind
                != semantic::vhdl::StatementKind::signal_assignment
            || !input.target
            || (input.waveform.empty() && !input.value)) {
            return false;
        }
        const auto value = input.waveform.empty()
            ? input.value
            : std::optional { input.waveform.front().value };
        return specialized_hir_unit_->find_expression(*input.target)
                   .has_value()
            && value
            && specialized_hir_unit_->find_expression(*value).has_value();
    }
    const auto& input = *source->systemverilog;
    if (input.kind != semantic::sv::StatementKind::assignment
        || input.assignment_kind
            != semantic::sv::AssignmentKind::continuous
        || input.assignment_control
            != semantic::sv::AssignmentControl::none
        || input.update_kind != semantic::sv::UpdateKind::none
        || input.delay || !input.target || !input.value) {
        return false;
    }
    return specialized_hir_unit_->find_expression(*input.target).has_value()
        && specialized_hir_unit_->find_expression(*input.value).has_value();
}

bool Lowerer::diagnose_hir_vhdl_block_guard(
    const semantic::ExpressionId expression)
{
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
    // See can_lower_hir_concurrent_statement: the statement's enclosing unit
    // scope is a name-resolution scope, not process-local storage.
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
    description.kind = language == frontend::Language::Vhdl2008
        ? frontend::ProcessKind::VhdlProcess
        : frontend::ProcessKind::SystemVerilogAlwaysComb;
    description.wildcard_sensitivity = true;
    description.require_wildcard_dependency = false;
    description.always_comb_or_latch = true;
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
    return result;
}

std::optional<Process> Lowerer::lower_hir_input_actual(
    const semantic::ExpressionId expression,
    const SignalId destination,
    const frontend::Language language,
    const std::string_view hierarchy,
    const std::size_t order,
    std::optional<semantic::vhdl::SubtypeIndication> vhdl_context)
{
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
    description.kind = language == frontend::Language::Vhdl2008
        ? frontend::ProcessKind::VhdlProcess
        : frontend::ProcessKind::SystemVerilogAlwaysComb;
    description.wildcard_sensitivity = true;
    description.require_wildcard_dependency = false;
    description.always_comb_or_latch = true;
    return lower_hir_process_body(description, language, hierarchy);
}

std::optional<Process> Lowerer::lower_hir_output_actual(
    const SignalId source,
    const semantic::ExpressionId expression,
    const frontend::Language language,
    const std::string_view hierarchy,
    const std::size_t order)
{
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
    const auto declaration = expression
        ? hir_target_declaration(target)
        : std::nullopt;
    const auto binding = declaration
        ? hir_runtime_binding(*declaration, hir_process_scope_, true)
        : std::nullopt;
    if (!expression || !binding || !binding->signal
        || read_only_signals_.contains(*binding->signal)) {
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
    if (selected
        && (operands.size() != (index ? 2U : 3U)
            || hir_referenced_declaration(operands.front())
                != declaration)) {
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
    const auto dynamic_part_width = slice && !constant_selection
        ? hir_dynamic_part_width(target, hir_process_scope_)
        : std::nullopt;
    const auto write_width = constant_selection
        ? std::optional { constant_selection->width }
        : index ? std::optional<std::size_t> { 1U }
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
        });
        return true;
    }
    if (!selected) {
        process_.operations.emplace_back(
            WriteUpdate { *binding->signal, source });
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
    if (index) {
        process_.operations.emplace_back(WriteUpdateDynamicSlice {
            *binding->signal, source, *dynamic });
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
    });
    return true;
}

std::optional<Process> Lowerer::lower_hir_process_body(
    const HirProcessDescription& description,
    const frontend::Language language,
    const std::string_view hierarchy)
{
    process_ = Process { };
    generated_processes_.clear();
    implicit_signal_dependencies_.clear();
    deferred_assertion_action_phase_.reset();
    deferred_assertion_action_handoff_.reset();
    language_ = language;
    sample_concurrent_assertion_reads_
        = description.concurrent_assertion;
    hierarchy_ = std::string { hierarchy };
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
    procedural_continuous_assignments_.clear();
    procedural_continuous_assignment_by_statement_.clear();
    procedural_continuous_assignments_by_target_.clear();
    process_.id = static_cast<ProcessId>(design_.processes_.size());
    hir_process_scope_ = description.scope;
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
    if (language == frontend::Language::SystemVerilog2017
        && !description.statements.empty()) {
        prepare_hir_procedural_continuous_assignments(
            description.statements);
    }

    std::optional<RegisterId> event_baseline;
    if (description.event_expression) {
        const auto width = hir_expression_width(
            *description.event_expression, hir_process_scope_);
        if (!width || *width == 0U || *width > 64U) {
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
    const auto resume_entry = static_cast<InstructionIndex>(
        process_.operations.size());
    std::optional<InstructionIndex> event_filter_branch;
    if (description.event_expression) {
        process_.operations.emplace_back(WaitSensitivity { });
        const auto width = register_width(*event_baseline);
        const auto current = lower_hir_expression(
            *description.event_expression, width);
        if (!current) {
            process_ = { };
            return std::nullopt;
        }
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
            const auto previous_forbidden = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            const auto current_forbidden = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Binary {
                BinaryOperator::case_equal,
                previous_forbidden,
                *event_baseline,
                description.event_edge
                        == runtime::simir::EdgeKind::posedge
                    ? one
                    : zero,
            });
            process_.operations.emplace_back(Binary {
                BinaryOperator::case_equal,
                current_forbidden,
                *current,
                description.event_edge
                        == runtime::simir::EdgeKind::posedge
                    ? zero
                    : one,
            });
            const auto previous_allowed = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            const auto current_allowed = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LogicalNot {
                previous_allowed, previous_forbidden });
            process_.operations.emplace_back(LogicalNot {
                current_allowed, current_forbidden });
            const auto first = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LogicalBinary {
                LogicalBinaryOperator::logical_and,
                first,
                matched,
                previous_allowed,
            });
            const auto edge_matched = allocate_register(
                1U, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LogicalBinary {
                LogicalBinaryOperator::logical_and,
                edge_matched,
                first,
                current_allowed,
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
        const auto active = allocate_register(
            1U, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(ReadSignal {
            active, *description.procedural_assignment_active });
        implicit_signal_dependencies_.push_back(
            *description.procedural_assignment_active);
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
    } else if (description.input_actual) {
        const auto destination = *description.input_actual_destination;
        const auto& signal = design_.signal_info_[destination];
        auto actual = *description.input_actual;
        auto actual_expression
            = specialized_hir_unit_->find_expression(actual);
        constexpr auto qualified_prefix
            = std::string_view { "@vhdl-qualified:" };
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
            : lower_hir_expression(actual, signal.width);
        if (!value) {
            process_ = { };
            return std::nullopt;
        }
        if (register_width(*value) != signal.width) {
            value = resize_register(
                *value, signal.width, signal.is_signed);
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
        process_.operations.emplace_back(WriteUpdate {
            destination, *value });
    } else if (description.output_actual) {
        const auto source = *description.output_actual_source;
        const auto& signal = design_.signal_info_[source];
        const auto value = allocate_register(
            signal.width, signal.source_domain);
        process_.operations.emplace_back(ReadSignal { value, source });
        implicit_signal_dependencies_.push_back(source);
        if (!lower_hir_output_actual_write(
                *description.output_actual, value)) {
            process_ = { };
            return std::nullopt;
        }
    } else if (description.vhdl_guarded_assignment) {
        if (!description.vhdl_guard) {
            report(
                "FSIM-ELAB-VHDLGUARD-001",
                "a guarded concurrent assignment requires an enclosing "
                "Boolean-guarded block",
                hir_source_span(description.source));
        } else {
            const auto guard_width = hir_expression_width(
                *description.vhdl_guard, hir_process_scope_)
                                         .value_or(1U);
            auto guard = lower_hir_expression(
                *description.vhdl_guard, guard_width);
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
            if (binding->domain != frontend::ValueDomain::Logic9) {
                report(
                    "FSIM-ELAB-VHDLGUARD-002",
                    "guarded driver disconnection requires a nine-state "
                    "resolved signal target",
                    hir_source_span(description.source));
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
    if (description.wildcard_sensitivity) {
        for (const auto dependency : implicit_signal_dependencies_) {
            process_.static_sensitivity.push_back(
                { dependency, runtime::simir::EdgeKind::any });
        }
        if (description.require_wildcard_dependency
            && !description.always_comb_or_latch
            && process_.static_sensitivity.empty()) {
            report(
                "FSIM-ELAB-061",
                "wildcard process sensitivity has no readable signal "
                "dependencies",
                hir_source_span(description.source));
        }
    }
    std::ranges::sort(
        process_.static_sensitivity,
        {},
        [&](const runtime::simir::Sensitivity& sensitivity) {
            const auto name = sensitivity.signal
                    < design_.signal_info_.size()
                ? std::string_view {
                      design_.signal_info_[sensitivity.signal].name }
                : std::string_view { };
            return std::pair {
                name,
                static_cast<std::uint8_t>(sensitivity.edge),
            };
        });
    auto duplicate = std::ranges::unique(process_.static_sensitivity);
    process_.static_sensitivity.erase(duplicate.begin(), duplicate.end());
    const auto waits_before_first_execution
        = description.wait_before_first_execution
        || (language != frontend::Language::Vhdl2008
            && !description.initial && !description.always_comb_or_latch
            && !process_.static_sensitivity.empty());
    if (waits_before_first_execution && !description.event_expression) {
        for (auto& operation : process_.operations) {
            relocate_instruction_targets(operation, resume_entry);
        }
        process_.operations.insert(
            process_.operations.cbegin() + resume_entry,
            WaitSensitivity { });
        ++callable_body_begin;
    }

    const auto executes_once = description.always_comb_or_latch
        && process_.static_sensitivity.empty();
    std::optional<InstructionIndex> vhdl_trailing_wait;
    std::vector<Operation> process_termination;
    if (description.initial || description.final || executes_once) {
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
    if (!process_.static_sensitivity.empty() && vhdl_explicit_wait) {
        report(
            "FSIM-VHDL-SEM-012",
            "a process sensitivity list cannot be combined with a direct or "
            "transitively called wait statement",
            hir_source_span(description.source));
    }
    validate_read_only_signal_writes(
        hir_source_span(description.source),
        description.input_actual_destination);
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
    return std::move(process_);
}

} // namespace fsim::elaboration
