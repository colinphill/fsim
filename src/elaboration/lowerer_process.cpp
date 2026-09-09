// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"
#include "lowerer_driver_regions.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

void Lowerer::set_systemverilog_program_owner(
    const std::optional<std::uint32_t> owner) noexcept
{
    systemverilog_program_owner_ = owner;
}

void Lowerer::set_systemverilog_standard(
    const frontend::StandardRevision standard) noexcept
{
    systemverilog_standard_ = standard;
}

void Lowerer::set_vhdl_standard(
    const frontend::VhdlStandard standard) noexcept
{
    vhdl_standard_ = standard;
}

void Lowerer::set_vhdl_synopsys_numeric_context(
    const bool signed_visible,
    const bool unsigned_visible) noexcept
{
    vhdl_synopsys_signed_visible_ = signed_visible;
    vhdl_synopsys_unsigned_visible_ = unsigned_visible;
}

namespace {

    [[nodiscard]] runtime::simir::OutputFormat runtime_output_format(
        const frontend::OutputFormat source)
    {
        switch (source) {
        case frontend::OutputFormat::Binary:
            return runtime::simir::OutputFormat::binary;
        case frontend::OutputFormat::Hexadecimal:
            return runtime::simir::OutputFormat::hexadecimal;
        case frontend::OutputFormat::Octal:
            return runtime::simir::OutputFormat::octal;
        case frontend::OutputFormat::Decimal:
            return runtime::simir::OutputFormat::decimal;
        case frontend::OutputFormat::Character:
            return runtime::simir::OutputFormat::character;
        case frontend::OutputFormat::String:
            return runtime::simir::OutputFormat::string;
        case frontend::OutputFormat::RealScientific:
            return runtime::simir::OutputFormat::real_scientific;
        case frontend::OutputFormat::RealFixed:
            return runtime::simir::OutputFormat::real_fixed;
        case frontend::OutputFormat::RealGeneral:
            return runtime::simir::OutputFormat::real_general;
        case frontend::OutputFormat::Time:
            return runtime::simir::OutputFormat::time;
        case frontend::OutputFormat::Hierarchy:
            break;
        }
        throw std::logic_error { "invalid frontend output format" };
    }

} // namespace

bool Lowerer::lower_delay_wait(
    const frontend::Delay& delay,
    const frontend::SourceSpan& span)
{
    if (!delay.expression) {
        process_.operations.emplace_back(WaitFor { delay.magnitude });
        return true;
    }
    const auto& expression = *delay.expression;
    const auto* type = expression.kind == frontend::ExpressionKind::Identifier
        ? object_type(expression.text)
        : nullptr;
    const auto scalar_kind = type != nullptr
        ? type->systemverilog_scalar
        : expression.systemverilog_scalar_kind;
    if (scalar_kind == frontend::SystemVerilogScalarKind::Chandle) {
        report(
            "FSIM-ELAB-SVDELAY-004",
            "a runtime delay requires an integral, time, real, shortreal, or "
            "realtime expression",
            span);
        return false;
    }
    const auto width = scalar_kind == frontend::SystemVerilogScalarKind::ShortReal
        ? std::size_t { 32 }
        : scalar_kind == frontend::SystemVerilogScalarKind::Real
            || scalar_kind == frontend::SystemVerilogScalarKind::Realtime
            || scalar_kind == frontend::SystemVerilogScalarKind::Time
        ? std::size_t { 64 }
        : infer_width(expression).value_or(std::size_t { 32 });
    const auto source = lower_expression(expression, width);
    if (!source) {
        report(
            "FSIM-ELAB-SVDELAY-004",
            "runtime delay expression cannot be lowered to a packed scalar",
            span);
        return false;
    }
    WaitFor wait;
    wait.delay = delay.magnitude;
    wait.source = *source;
    wait.source_width = static_cast<std::uint32_t>(register_width(*source));
    wait.source_kind = scalar_kind;
    wait.source_signed = is_signed_expression(expression);
    wait.rounding_quantum = delay.rounding_quantum;
    process_.operations.emplace_back(wait);
    return true;
}

Lowerer::Lowerer(
    ElaboratedDesign& design,
    const std::unordered_map<std::string, SignalId>& signals,
    const std::unordered_set<SignalId>& read_only_signals,
    const std::unordered_map<std::string, StringObjectId>&
        string_objects,
    const std::unordered_set<StringObjectId>&
        read_only_string_objects,
    const std::unordered_map<std::string, ContainerObjectId>&
        container_objects,
    const std::unordered_set<std::string>&
        read_only_container_objects,
    const std::unordered_map<
        std::string, const frontend::Type*>& visible_types,
    const std::unordered_map<
        std::string, const frontend::Type*>& visible_type_marks,
    const std::vector<frontend::FunctionDeclaration>& functions,
    const std::vector<frontend::TaskDeclaration>& tasks,
    const std::vector<frontend::ProcedureDeclaration>& procedures,
    const frontend::SystemVerilogScalarEvaluationContext scalar_context,
    std::vector<Diagnostic>& diagnostics)
    : design_(design)
    , signals_(signals)
    , read_only_signals_(read_only_signals)
    , string_objects_(string_objects)
    , read_only_string_objects_(read_only_string_objects)
    , container_objects_(container_objects)
    , read_only_container_objects_(
          read_only_container_objects)
    , visible_types_(visible_types)
    , visible_type_marks_(visible_type_marks)
    , functions_(functions)
    , tasks_(tasks)
    , procedures_(procedures)
    , scalar_context_(scalar_context)
    , diagnostics_(diagnostics)
{
}

bool Lowerer::report_unsupported_cross_root_reference(
    const std::string_view name,
    const frontend::SourceSpan span)
{
    auto normalized = name;
    constexpr std::string_view root_prefix { "$root." };
    if (normalized.starts_with(root_prefix)) {
        normalized.remove_prefix(root_prefix.size());
    }
    const auto hierarchy_separator = hierarchy_.find('.');
    const auto owning_root = std::string_view { hierarchy_ }.substr(
        0, hierarchy_separator);
    for (const auto& root : design_.roots()) {
        if (root == owning_root
            || !normalized.starts_with(root + ".")) {
            continue;
        }
        report(
            "FSIM-ELAB-ROOT-001",
            language_ == frontend::Language::SystemVerilog2017
                ? "unsupported cross-root hierarchical shortcut '"
                    + std::string { name }
                    + "'; multiple roots expose root-level packed signals "
                      "through SystemVerilog top-level hierarchical names, "
                      "while descendant state must be connected through a "
                      "root-level port or signal"
                : "cross-root reference '" + std::string { name }
                    + "' is not a language-defined global mechanism for this "
                      "source language",
            span);
        return true;
    }
    return false;
}

Process Lowerer::lower_process(
    const frontend::Process& source,
    const frontend::Language language,
    const std::string_view hierarchy)
{
    process_ = Process { };
    generated_processes_.clear();
    implicit_signal_dependencies_.clear();
    language_ = language;
    process_kind_ = source.kind;
    hierarchy_ = std::string { hierarchy };
    next_register_ = 0;
    next_string_register_ = 0;
    next_container_register_ = 0;
    register_widths_.clear();
    register_domains_.clear();
    locals_.clear();
    string_locals_.clear();
    container_locals_.clear();
    vhdl_access_heaps_.clear();
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
    process_.name = std::string(hierarchy) + "."
        + (source.name.empty()
                ? "process_" + std::to_string(process_.id)
                : source.name);
    process_.final = source.kind == ProcessKind::Final;
    process_.postponed = source.vhdl_postponed;
    if (process_.final) {
        process_.initialize = false;
    }
    prepare_procedural_continuous_assignments(source.statements);
    class_tasks_.clear();
    collect_class_tasks(source.statements);
    initialize_function_support();
    initialize_task_support();
    initialize_procedure_support();
    initialize_variables(source.variables);
    bool wildcard_sensitivity = false;
    const frontend::Sensitivity* general_sensitivity = nullptr;
    for (const auto& sensitivity : source.sensitivities) {
        if (sensitivity.signal == "*") {
            wildcard_sensitivity = true;
            continue;
        }
        if (sensitivity.expression.valid()) {
            if (language == frontend::Language::Vhdl2008) {
                const auto dependency_start = implicit_signal_dependencies_.size();
                const auto width = infer_width(
                    sensitivity.expression);
                if (width && *width != 0) {
                    static_cast<void>(lower_expression(
                        sensitivity.expression, *width));
                }
                for (auto index = dependency_start;
                    index < implicit_signal_dependencies_.size();
                    ++index) {
                    process_.static_sensitivity.push_back({ implicit_signal_dependencies_[index],
                        runtime::simir::EdgeKind::any });
                }
                continue;
            }
            if (general_sensitivity != nullptr
                || source.sensitivities.size() != 1) {
                report(
                    "FSIM-ELAB-SVEVENT-002",
                    "packed process event-expression metadata is "
                    "mixed with another event",
                    sensitivity.span);
                continue;
            }
            general_sensitivity = &sensitivity;
            std::set<std::string> dependencies;
            collect_identifiers(
                sensitivity.expression, dependencies);
            for (const auto& dependency : dependencies) {
                if (locals_.contains(dependency)) {
                    continue;
                }
                if (const auto found = signals_.find(dependency);
                    found != signals_.end()) {
                    process_.static_sensitivity.push_back(
                        { found->second,
                            runtime::simir::EdgeKind::any });
                }
            }
            continue;
        }
        const auto found = signals_.find(sensitivity.signal);
        if (found == signals_.end()) {
            report(
                "FSIM-ELAB-020",
                "unknown sensitivity signal '" + sensitivity.signal + "'",
                sensitivity.span);
            continue;
        }
        runtime::simir::EdgeKind edge = runtime::simir::EdgeKind::any;
        if (sensitivity.edge == frontend::EdgeKind::Positive) {
            edge = runtime::simir::EdgeKind::posedge;
        } else if (sensitivity.edge == frontend::EdgeKind::Negative) {
            edge = runtime::simir::EdgeKind::negedge;
        }
        process_.static_sensitivity.push_back({ found->second, edge });
    }
    if (wildcard_sensitivity) {
        std::set<std::string> dependencies;
        collect_wildcard_identifiers(
            source.statements, dependencies);
        for (const auto& dependency : dependencies) {
            if (locals_.contains(dependency)) {
                continue;
            }
            if (const auto found = signals_.find(dependency);
                found != signals_.end()) {
                process_.static_sensitivity.push_back(
                    { found->second,
                        runtime::simir::EdgeKind::any });
            }
        }
    }
    if (general_sensitivity != nullptr
        && process_.static_sensitivity.empty()) {
        report(
            "FSIM-ELAB-SVEVENT-001",
            "packed process event expression has no readable signal "
            "dependencies",
            general_sensitivity->span);
    }

    const bool verilog_event_process = language != frontend::Language::Vhdl2008
        && source.kind != ProcessKind::Initial
        && source.kind
            != ProcessKind::SystemVerilogAlwaysComb
        && source.kind
            != ProcessKind::SystemVerilogAlwaysLatch
        && !process_.static_sensitivity.empty();
    const bool body_timed_always = language != frontend::Language::Vhdl2008
        && source.kind == ProcessKind::VerilogAlways
        && source.sensitivities.empty();
    const Statement* vhdl_edge_guard = language == frontend::Language::Vhdl2008
        ? recognized_vhdl_edge_guard(source)
        : nullptr;
    const bool waits_before_first_execution = verilog_event_process || vhdl_edge_guard != nullptr;
    const bool vhdl_explicit_wait = language == frontend::Language::Vhdl2008
        && contains_explicit_wait(source.statements);
    std::optional<InstructionIndex> vhdl_trailing_wait;
    const auto resume_entry = static_cast<InstructionIndex>(process_.operations.size());
    std::optional<InstructionIndex> event_wait_entry;
    std::optional<InstructionIndex> event_filter_branch;
    if (general_sensitivity != nullptr) {
        const auto width = infer_width(
            general_sensitivity->expression);
        if (!width || *width == 0 || *width > 64) {
            report(
                "FSIM-ELAB-SVEVENT-003",
                "packed process event expression must have an "
                "executable width from 1 through 64 bits",
                general_sensitivity->span);
        } else if (const auto baseline = lower_expression(
                       general_sensitivity->expression, *width)) {
            event_wait_entry = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(WaitSensitivity { });
            const auto current = lower_expression(
                general_sensitivity->expression, *width);
            if (current) {
                const auto equal = allocate_register(
                    1, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(Binary {
                    BinaryOperator::case_equal,
                    equal,
                    *baseline,
                    *current });
                process_.operations.emplace_back(
                    CopyRegister { *baseline, *current });
                event_filter_branch = static_cast<InstructionIndex>(
                    process_.operations.size());
                process_.operations.emplace_back(Branch {
                    equal, 0, 0,
                    UnknownBranchPolicy::when_false });
            }
        }
    } else if (waits_before_first_execution) {
        process_.operations.emplace_back(WaitSensitivity { });
    }
    const auto body_entry = static_cast<InstructionIndex>(
        process_.operations.size());
    if (event_filter_branch && event_wait_entry) {
        process_.operations[*event_filter_branch] = Branch {
            fsim::runtime::simir::operation_get<Branch>(
                process_.operations[*event_filter_branch])
                .condition,
            *event_wait_entry,
            body_entry,
            UnknownBranchPolicy::when_false
        };
    }
    emit_debug_point(DebugPointKind::process_entry, source.span);
    if (vhdl_edge_guard != nullptr) {
        // The frontend refines the sensitivity edge from this canonical
        // idiom. The scheduler now enforces the predicate, so lower only
        // the taken body and retain any following statements.
        lower_statements(vhdl_edge_guard->statements);
        for (std::size_t index = 1; index < source.statements.size(); ++index) {
            lower_statement(source.statements[index]);
        }
    } else {
        lower_statements(source.statements);
    }
    if (wildcard_sensitivity) {
        for (const auto dependency : implicit_signal_dependencies_) {
            process_.static_sensitivity.push_back(
                { dependency, runtime::simir::EdgeKind::any });
        }
        if (process_.static_sensitivity.empty()) {
            report(
                "FSIM-ELAB-061",
                "wildcard process sensitivity has no readable signal "
                "dependencies",
                source.span);
        }
    }
    if (source.kind == ProcessKind::Initial
        || source.kind == ProcessKind::Final) {
        process_.operations.emplace_back(Halt { });
    } else if (waits_before_first_execution) {
        // Event-controlled processes wait before their first execution.
        // Returning directly to operation zero preserves one body
        // execution per matching event.
        process_.operations.emplace_back(Jump {
            event_wait_entry.value_or(resume_entry) });
    } else if (body_timed_always) {
        process_.operations.emplace_back(Jump { resume_entry });
    } else if (
        language == frontend::Language::Vhdl2008
        && source.sensitivities.empty()) {
        // Reserve the implicit trailing sensitivity point until exact,
        // overload-resolved procedure dependencies have been lowered.
        // A direct or transitively called wait replaces this first
        // operation with the process-repeat jump below.
        vhdl_trailing_wait = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(WaitSensitivity { });
        process_.operations.emplace_back(Jump { resume_entry });
    } else {
        // A VHDL process with a sensitivity list executes once at time
        // zero, then waits at the implicit trailing sensitivity point.
        process_.operations.emplace_back(WaitSensitivity { });
        process_.operations.emplace_back(Jump { resume_entry });
    }
    do {
        lower_pending_tasks();
        lower_pending_procedures();
        lower_pending_functions();
    } while (!pending_tasks_.empty()
        || !pending_procedures_.empty()
        || !pending_functions_.empty());
    const bool vhdl_procedure_wait = language == frontend::Language::Vhdl2008
        && procedure_dependencies_suspend(
            process_procedure_dependencies_);
    if (vhdl_trailing_wait
        && (vhdl_explicit_wait || vhdl_procedure_wait)) {
        process_.operations[*vhdl_trailing_wait] = Jump { resume_entry };
    }
    if (language == frontend::Language::Vhdl2008
        && !source.sensitivities.empty()
        && (vhdl_explicit_wait || vhdl_procedure_wait)) {
        report(
            "FSIM-VHDL-SEM-012",
            "a process sensitivity list cannot be combined with a "
            "direct or transitively called wait statement",
            source.span);
    }
    for (std::size_t index = 0;
        index < function_procedure_dependencies_.size();
        ++index) {
        if (procedure_dependencies_suspend(
                function_procedure_dependencies_[index])) {
            report(
                "FSIM-ELAB-VHLEGAL-009",
                "VHDL function '"
                    + function_frames_[index].source->name
                    + "' calls a suspending procedure",
                function_frames_[index].source->span);
        }
    }
    validate_read_only_signal_writes(source.span);
    process_.register_count = next_register_;
    process_.string_register_count = next_string_register_;
    process_.container_register_count = next_container_register_;
    process_.register_value_kinds.reserve(register_domains_.size());
    for (const auto domain : register_domains_) {
        process_.register_value_kinds.push_back(
            value_kind(domain));
    }
    process_.driver_regions = collect_driver_regions(
        process_, register_widths_);
    validate_vhdl_driver_attributes(source.span);
    materialize_procedural_continuous_assignments();
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

[[nodiscard]] const frontend::Type* Lowerer::visible_type(
    const std::string_view name) const
{
    const auto found = visible_types_.find(std::string { name });
    return found == visible_types_.end()
        ? nullptr
        : found->second;
}

[[nodiscard]] const frontend::Type* Lowerer::object_type(
    const std::string_view name) const
{
    if (const auto local = local_types_.find(std::string { name });
        local != local_types_.end()) {
        return local->second;
    }
    if (const auto* type = visible_type(name);
        type != nullptr) {
        return type;
    }
    if (language_ != frontend::Language::Vhdl2008) {
        return nullptr;
    }
    const auto selected = packed_member_reference(name);
    if (!selected || selected->member->nested_types.empty()) {
        return nullptr;
    }
    return &selected->member->nested_types.front();
}

[[nodiscard]] const frontend::Type* Lowerer::visible_type_mark(
    const std::string_view name) const
{
    if (active_function_) {
        const auto* function = function_frames_[*active_function_].source;
        if (function != nullptr
            && function->vhdl_return_identifier == name) {
            return &function->return_type;
        }
    }
    const auto found = visible_type_marks_.find(std::string { name });
    if (found != visible_type_marks_.end()) {
        return found->second;
    }
    if (vhdl_standard_ >= frontend::VhdlStandard::Vhdl2019) {
        static const auto reflection_type_class =
            frontend::vhdl_reflection_type("type_class");
        static const auto reflection_value_class =
            frontend::vhdl_reflection_type("value_class");
        if (name == "type_class") {
            return &*reflection_type_class;
        }
        if (name == "value_class") {
            return &*reflection_value_class;
        }
    }
    if (vhdl_standard_ >= frontend::VhdlStandard::Vhdl1993
        && name == "file_open_kind") {
        static const auto file_open_kind = [] {
            frontend::Type type;
            type.domain = frontend::ValueDomain::Bit2;
            type.spelling = "file_open_kind";
            type.nominal_type = "@builtin:file_open_kind";
            type.vhdl_type_declaration = type.nominal_type;
            type.packed_range = frontend::PackedRange { 1, 0, true };
            type.enumeration_literals = {
                "read_mode", "write_mode", "append_mode" };
            type.enumeration_range = frontend::EnumerationRange { 0, 2, false };
            return type;
        }();
        return &file_open_kind;
    }
    return nullptr;
}

[[nodiscard]] const frontend::Type*
Lowerer::enumeration_expression_type(
    const Expression& expression) const
{
    if (expression.kind == ExpressionKind::LogicLiteral
        && expression.text.starts_with("@fsim-enum:")
        && !expression.nominal_type.empty()) {
        const auto found = std::find_if(
            visible_type_marks_.begin(),
            visible_type_marks_.end(),
            [&](const auto& entry) {
                return entry.second != nullptr
                    && entry.second->nominal_type
                    == expression.nominal_type;
            });
        return found == visible_type_marks_.end()
            ? nullptr
            : found->second;
    }
    if (expression.kind == ExpressionKind::Identifier) {
        const auto* type = object_type(expression.text);
        return type != nullptr
                && !type->enumeration_literals.empty()
            ? type
            : nullptr;
    }
    if (expression.kind != ExpressionKind::Call
        || expression.operands.empty()
        || expression.operands.front().kind
            != ExpressionKind::Identifier) {
        return nullptr;
    }
    const bool enumeration_result = expression.text == "'left"
        || expression.text == "'right"
        || expression.text == "'low"
        || expression.text == "'high"
        || expression.text == "'val"
        || expression.text == "'succ"
        || expression.text == "'pred"
        || expression.text == "'leftof"
        || expression.text == "'rightof";
    if (!enumeration_result) {
        return nullptr;
    }
    const auto* type = visible_type_mark(
        expression.operands.front().text);
    return type != nullptr
            && !type->enumeration_literals.empty()
        ? type
        : nullptr;
}

[[nodiscard]] const frontend::Type*
Lowerer::systemverilog_expression_type(
    const Expression& expression) const
{
    if (language_ != frontend::Language::SystemVerilog2017) {
        return nullptr;
    }
    if (!expression.nominal_type.empty()) {
        const auto found = std::ranges::find_if(
            visible_type_marks_,
            [&](const auto& entry) {
                return entry.second != nullptr
                    && entry.second->nominal_type
                    == expression.nominal_type;
            });
        if (found != visible_type_marks_.end()) {
            return found->second;
        }
    }
    if (expression.kind == ExpressionKind::Identifier) {
        if (const auto* direct = object_type(expression.text)) {
            return direct;
        }
        if (const auto selected = packed_member_reference(expression.text);
            selected && !selected->member->nested_types.empty()) {
            return &selected->member->nested_types.front();
        }
        return nullptr;
    }
    if (expression.kind == ExpressionKind::Index) {
        const Expression* base = &expression;
        std::size_t indices = 0;
        while (base->kind == ExpressionKind::Index
            && !base->operands.empty()) {
            ++indices;
            base = &base->operands.front();
        }
        const auto* container_type = systemverilog_expression_type(*base);
        if (container_type == nullptr
            || !container_type->systemverilog_container
            || container_type->systemverilog_container
                ->element_types.empty()) {
            return nullptr;
        }
        const auto& container = *container_type->systemverilog_container;
        const auto required_indices = container.kind
                == frontend::SystemVerilogContainerKind::StaticArray
            ? std::max<std::size_t>(
                  1U, container.static_range_expressions.size())
            : 1U;
        return indices >= required_indices
            ? &container.element_types.front()
            : nullptr;
    }
    if (expression.kind != ExpressionKind::Call) {
        return nullptr;
    }
    if (expression.text.starts_with("@sv-cast:")) {
        return visible_type_mark(
            std::string_view { expression.text }.substr(
                std::string_view { "@sv-cast:" }.size()));
    }
    if (expression.text == "?:"
        && expression.operands.size() == 3U) {
        const auto* when_true = systemverilog_expression_type(expression.operands[1]);
        const auto* when_false = systemverilog_expression_type(expression.operands[2]);
        return when_true != nullptr && when_false != nullptr
                && when_true->nominal_type
                    == when_false->nominal_type
            ? when_true
            : nullptr;
    }
    if (const auto* function = visible_function(expression.text)) {
        return &function->return_type;
    }
    return nullptr;
}

[[nodiscard]] std::pair<std::int64_t, std::int64_t>
Lowerer::integer_bounds(
    const std::optional<frontend::IntegerRange>& range) const
{
    if (!range) {
        const auto predefined = frontend::vhdl_predefined_integer_range(
            vhdl_standard_, "integer");
        return { predefined.left, predefined.right };
    }
    return {
        std::min(range->left, range->right),
        std::max(range->left, range->right)
    };
}

void Lowerer::emit_integer_check(
    const RegisterId source,
    const std::optional<frontend::IntegerRange>& range)
{
    const auto [lower, upper] = integer_bounds(range);
    process_.operations.emplace_back(
        IntegerCheck { source, lower, upper });
}

[[nodiscard]] std::pair<std::int32_t, std::int32_t>
Lowerer::enumeration_bounds(const frontend::Type& type)
{
    if (type.enumeration_range) {
        return {
            static_cast<std::int32_t>(
                std::min(
                    type.enumeration_range->left,
                    type.enumeration_range->right)),
            static_cast<std::int32_t>(
                std::max(
                    type.enumeration_range->left,
                    type.enumeration_range->right))
        };
    }
    return {
        0,
        static_cast<std::int32_t>(
            type.enumeration_literals.size() - 1U)
    };
}

void Lowerer::emit_enumeration_check(
    const RegisterId source,
    const frontend::Type& type)
{
    const auto ordinal = widen_enumeration_ordinal(source);
    const auto [lower, upper] = enumeration_bounds(type);
    process_.operations.emplace_back(
        IntegerCheck { ordinal, lower, upper });
}

[[nodiscard]] bool
Lowerer::validate_static_enumeration_assignment(
    const Expression& expression,
    const frontend::Type& type,
    const frontend::SourceSpan& span)
{
    const auto ordinal = vhdl_enumeration_ordinal(expression, type);
    if (!ordinal) {
        return true;
    }
    const auto [lower, upper] = enumeration_bounds(type);
    if (*ordinal < lower || *ordinal > upper) {
        report(
            "FSIM-ELAB-VHENUMRANGE-004",
            "locally static VHDL enumeration value '"
                + expression.text
                + "' is outside subtype range '"
                + type.enumeration_literals[static_cast<std::size_t>(lower)]
                + "' to '"
                + type.enumeration_literals[static_cast<std::size_t>(upper)]
                + "'",
            span);
        return false;
    }
    return true;
}

[[nodiscard]] bool Lowerer::validate_static_integer_assignment(
    const Expression& expression,
    const std::optional<frontend::IntegerRange>& range,
    const frontend::SourceSpan& span)
{
    std::string error;
    const auto value = evaluate_constant_expression(expression, { }, error);
    if (!value) {
        return true;
    }
    const auto [lower, upper] = integer_bounds(range);
    if (*value < lower || *value > upper) {
        report(
            "FSIM-ELAB-INTEGER-003",
            "locally static VHDL integer value "
                + std::to_string(*value)
                + " is outside subtype range "
                + std::to_string(lower) + " to "
                + std::to_string(upper),
            span);
        return false;
    }
    return true;
}

bool Lowerer::contains_explicit_wait(
    const std::vector<Statement>& statements)
{
    return std::any_of(
        statements.begin(), statements.end(),
        [](const Statement& statement) {
            const auto case_wait = std::any_of(
                statement.case_alternatives.begin(),
                statement.case_alternatives.end(),
                [](const frontend::CaseAlternative& alternative) {
                    return contains_explicit_wait(
                        alternative.statements);
                });
            return statement.kind == StatementKind::Delay
                || statement.kind == StatementKind::WaitOn
                || statement.kind == StatementKind::WaitUntil
                || statement.kind == StatementKind::WaitOrder
                || contains_explicit_wait(statement.statements)
                || contains_explicit_wait(statement.else_statements)
                || case_wait;
        });
}

[[nodiscard]] std::string Lowerer::declaration_key(
    const frontend::VariableDeclaration& variable) const
{
    auto key = variable.span.source_name + ":"
        + std::to_string(variable.span.begin.offset) + ":"
        + variable.name;
    if (active_function_) {
        const auto& frame = function_frames_[*active_function_];
        if (!frame.source->specialization_identity.empty()) {
            key += ":function-specialization:"
                + frame.source->specialization_identity;
        }
    }
    return key;
}

[[nodiscard]] std::string Lowerer::scoped_local_name(
    const std::string_view name) const
{
    std::string result;
    for (const auto& scope : local_scope_) {
        if (!result.empty()) {
            result += ".";
        }
        result += scope;
    }
    if (!result.empty()) {
        result += ".";
    }
    result += name;
    return result;
}

[[nodiscard]] std::string Lowerer::block_scope_name(
    const Statement& statement)
{
    if (!statement.label.empty()) {
        return statement.label;
    }
    return "$block_"
        + std::to_string(statement.span.begin.line) + "_"
        + std::to_string(statement.span.begin.column) + "_"
        + std::to_string(statement.span.begin.offset);
}

[[nodiscard]] std::string Lowerer::debug_scope_name() const
{
    auto result = process_.name;
    for (const auto& scope : local_scope_) {
        if (!result.empty()) {
            result += ".";
        }
        result += scope;
    }
    return result;
}

[[nodiscard]] std::string Lowerer::vhdl_statement_scope_name(
    const Statement& statement) const
{
    if (language_ != frontend::Language::Vhdl2008
        || statement.kind == StatementKind::Block) {
        return { };
    }
    if (!statement.label.empty()) {
        return statement.label;
    }
    if (statement.kind == StatementKind::Loop) {
        return "$loop_"
            + std::to_string(statement.span.begin.line) + "_"
            + std::to_string(statement.span.begin.column) + "_"
            + std::to_string(statement.span.begin.offset);
    }
    return { };
}

void Lowerer::lower_case_alternative(
    const frontend::CaseAlternative& alternative)
{
    const bool scoped = language_ == frontend::Language::Vhdl2008;
    if (scoped) {
        local_scope_.push_back(
            "$when_"
            + std::to_string(alternative.span.begin.line) + "_"
            + std::to_string(alternative.span.begin.column) + "_"
            + std::to_string(alternative.span.begin.offset));
    }
    lower_statements(alternative.statements);
    if (scoped) {
        local_scope_.pop_back();
    }
}

void Lowerer::lower_block(const Statement& statement)
{
    auto outer_locals = locals_;
    auto outer_string_locals = string_locals_;
    auto outer_container_locals = container_locals_;
    auto outer_signed = local_signed_;
    auto outer_ranges = local_ranges_;
    auto outer_integer_ranges = local_integer_ranges_;
    auto outer_members = local_members_;
    auto outer_types = local_types_;
    const auto declaration_scope = local_scope_;
    const auto block_begin = static_cast<InstructionIndex>(
        process_.operations.size());
    const auto function_returns_begin = function_return_jumps_.size();
    const auto procedure_returns_begin = procedure_return_jumps_.size();
    local_scope_.push_back(block_scope_name(statement));
    const bool named = !statement.label.empty();
    std::optional<std::size_t> named_control_index;
    if (named) {
        block_controls_.push_back({ statement.label, { }, std::nullopt });
        named_control_index = named_block_controls_.size();
        named_block_controls_.push_back(
            { statement.label, declaration_scope, block_begin, std::nullopt, { } });
    }
    const bool automatic_scope = !(active_function_
                                     && !function_frames_[*active_function_].source->automatic)
        && !(active_task_
            && !task_frames_[*active_task_].source->automatic);
    if (active_function_
        && !function_frames_[*active_function_].source->automatic) {
        bind_static_callable_variables(
            statement.declarations,
            function_frames_[*active_function_].static_variables);
    } else if (
        active_task_
        && !task_frames_[*active_task_].source->automatic) {
        bind_static_callable_variables(
            statement.declarations,
            task_frames_[*active_task_].static_variables);
    } else {
        initialize_variables(statement.declarations);
    }
    lower_statements(statement.statements);
    if (named) {
        auto control = std::move(block_controls_.back());
        block_controls_.pop_back();
        const auto epilogue = static_cast<InstructionIndex>(
            process_.operations.size());
        for (const auto jump : control.disable_jumps) {
            process_.operations[jump] = Jump { epilogue };
        }
        auto& named_control = named_block_controls_.at(*named_control_index);
        named_control.end = epilogue;
        for (const auto operation : named_control.disable_operations) {
            process_.operations[operation] = DisableBlock {
                named_control.begin, epilogue
            };
        }
    }
    const auto emit_cleanup = [&] {
        for (const auto& variable : statement.declarations) {
            if (variable.vhdl_file || variable.type.vhdl_file) {
                const auto file = locals_.find(variable.name);
                if (file != locals_.end()) {
                    process_.operations.emplace_back(
                        FileClose { file->second, true, true });
                }
            }
        }
        if (automatic_scope) {
            emit_vhdl_access_scope_cleanup(statement.declarations);
        }
    };
    emit_cleanup();

    const auto route_returns_through_cleanup =
        [&](std::vector<InstructionIndex>& returns,
            const std::size_t first) {
            if (language_ != frontend::Language::Vhdl2008
                || first >= returns.size()) {
                return;
            }
            const auto normal_exit = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Jump { });
            const auto return_cleanup = static_cast<InstructionIndex>(
                process_.operations.size());
            emit_cleanup();
            const auto continuation = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Jump { });
            for (auto index = first; index < returns.size(); ++index) {
                process_.operations[returns[index]] = Jump {
                    return_cleanup
                };
            }
            returns.erase(
                returns.begin()
                    + static_cast<std::ptrdiff_t>(first),
                returns.end());
            returns.push_back(continuation);
            process_.operations[normal_exit] = Jump {
                static_cast<InstructionIndex>(
                    process_.operations.size())
            };
        };
    if (active_function_) {
        route_returns_through_cleanup(
            function_return_jumps_, function_returns_begin);
    } else if (active_procedure_) {
        route_returns_through_cleanup(
            procedure_return_jumps_, procedure_returns_begin);
    }
    local_scope_.pop_back();
    locals_ = std::move(outer_locals);
    string_locals_ = std::move(outer_string_locals);
    container_locals_ = std::move(outer_container_locals);
    local_signed_ = std::move(outer_signed);
    local_ranges_ = std::move(outer_ranges);
    local_integer_ranges_ = std::move(outer_integer_ranges);
    local_members_ = std::move(outer_members);
    local_types_ = std::move(outer_types);
}

void Lowerer::lower_event_trigger(const Statement& statement)
{
    if (statement.target.kind != ExpressionKind::Identifier) {
        report(
            "FSIM-ELAB-099",
            "named-event trigger requires a simple event name",
            statement.span);
        return;
    }
    const auto found = signals_.find(statement.target.text);
    if (found == signals_.end()) {
        report(
            "FSIM-ELAB-100",
            "unknown named event '" + statement.target.text + "'",
            statement.target.span);
        return;
    }
    const auto& info = design_.signal_info_.at(found->second);
    if (info.type_name != "event") {
        report(
            "FSIM-ELAB-101",
            "event trigger target '" + statement.target.text
                + "' is not declared as an event",
            statement.target.span);
        return;
    }
    const auto current = allocate_register(1, frontend::ValueDomain::Logic4);
    const auto toggled = allocate_register(1, frontend::ValueDomain::Logic4);
    process_.operations.emplace_back(
        ReadSignal { current, found->second });
    process_.operations.emplace_back(
        UnaryNot { toggled, current });
    if (statement.assignment_kind
        == AssignmentKind::NonBlocking) {
        if (statement.delay) {
            process_.operations.emplace_back(
                WriteAfter {
                    found->second,
                    toggled,
                    statement.delay->magnitude });
        } else {
            process_.operations.emplace_back(
                WriteUpdate { found->second, toggled });
        }
    } else {
        process_.operations.emplace_back(
            WriteBlocking { found->second, toggled });
    }
}

const Statement* Lowerer::recognized_vhdl_edge_guard(
    const frontend::Process& source)
{
    if (source.statements.empty()) {
        return nullptr;
    }
    const auto& statement = source.statements.front();
    if (statement.kind != StatementKind::If
        || statement.condition.kind != ExpressionKind::Call
        || statement.condition.operands.size() != 1
        || statement.condition.operands.front().kind
            != ExpressionKind::Identifier) {
        return nullptr;
    }
    const auto& callee = statement.condition.text;
    const auto expected_edge = callee == "rising_edge"
        ? frontend::EdgeKind::Positive
        : callee == "falling_edge"
        ? frontend::EdgeKind::Negative
        : frontend::EdgeKind::Any;
    if (expected_edge == frontend::EdgeKind::Any) {
        return nullptr;
    }
    const auto& signal = statement.condition.operands.front().text;
    const auto matching = std::find_if(
        source.sensitivities.begin(),
        source.sensitivities.end(),
        [&](const frontend::Sensitivity& sensitivity) {
            return sensitivity.signal == signal
                && sensitivity.edge == expected_edge;
        });
    return matching == source.sensitivities.end() ? nullptr : &statement;
}

void Lowerer::lower_statements(const std::vector<Statement>& statements)
{
    for (const auto& statement : statements) {
        lower_statement(statement);
    }
}

void Lowerer::lower_statement(const Statement& statement)
{
    const auto statement_scope = vhdl_statement_scope_name(statement);
    if (!statement_scope.empty()) {
        local_scope_.push_back(statement_scope);
    }
    if (statement.kind != StatementKind::Block
        && !(statement.kind == StatementKind::Loop
            && statement.loop_runtime)) {
        auto kind = DebugPointKind::statement;
        if (statement.kind == StatementKind::Assert) {
            kind = DebugPointKind::assertion;
        } else if (
            statement.kind == StatementKind::Display
            || statement.kind == StatementKind::FileClose
            || statement.kind == StatementKind::FileFlush
            || statement.kind == StatementKind::FileDisplay
            || statement.kind == StatementKind::MemoryLoad
            || statement.kind == StatementKind::Report
            || statement.kind == StatementKind::TaskCall
            || statement.kind == StatementKind::ProcedureCall) {
            kind = DebugPointKind::call;
        } else if (
            statement.kind == StatementKind::Delay
            || statement.kind == StatementKind::WaitOn
            || statement.kind == StatementKind::WaitUntil
            || statement.kind == StatementKind::WaitOrder) {
            kind = DebugPointKind::wait;
        }
        emit_debug_point(kind, statement.span);
    }
    switch (statement.kind) {
    case StatementKind::Assignment:
        lower_assignment(statement);
        break;
    case StatementKind::Force:
    case StatementKind::Release:
        lower_force_release(statement);
        break;
    case StatementKind::ProceduralAssign:
    case StatementKind::Deassign:
        lower_procedural_continuous_assignment(statement);
        break;
    case StatementKind::ContainerMethod:
        lower_container_method(statement);
        break;
    case StatementKind::If:
        lower_if(statement);
        break;
    case StatementKind::Case:
        lower_case(statement);
        break;
    case StatementKind::Loop:
        lower_loop(statement);
        break;
    case StatementKind::Break:
        lower_loop_control(statement, true);
        break;
    case StatementKind::Continue:
        lower_loop_control(statement, false);
        break;
    case StatementKind::Return:
        if (active_task_) {
            lower_task_return(statement);
        } else if (active_procedure_) {
            lower_procedure_return(statement);
        } else {
            lower_function_return(statement);
        }
        break;
    case StatementKind::TaskCall:
        if (!lower_synchronization_method_statement(statement)
            && !lower_process_method_statement(statement)) {
            lower_task_call(statement);
        }
        break;
    case StatementKind::ProcedureCall:
        lower_procedure_call(statement);
        break;
    case StatementKind::Assert:
        lower_assert(statement);
        break;
    case StatementKind::Delay:
        if (!statement.delay) {
            report("FSIM-ELAB-030", "delay statement has no delay", statement.span);
            break;
        }
        if (lower_delay_wait(*statement.delay, statement.span)) {
            lower_statements(statement.statements);
        }
        break;
    case StatementKind::WaitOn: {
        if (emit_event_control_wait(statement)
            && statement.clocking_cycle_delay
            && !systemverilog_program_owner_) {
            // A clocking-cycle wait synchronizes module code with the
            // clocking block's observed input sample. Program processes are
            // already resumed in the reactive region; other processes need
            // an explicit region handoff before consuming clocking inputs or
            // scheduling clocking outputs.
            process_.operations.emplace_back(
                WaitRegion { runtime::SchedulerPhase::reactive });
        }
        lower_statements(statement.statements);
        break;
    }
    case StatementKind::WaitUntil:
        lower_wait_until(statement);
        break;
    case StatementKind::WaitOrder:
        lower_wait_order(statement);
        break;
    case StatementKind::EventTrigger:
        lower_event_trigger(statement);
        break;
    case StatementKind::MonitorControl:
        process_.operations.emplace_back(
            MonitorControl { statement.monitor_enabled });
        break;
    case StatementKind::FileClose: {
        if (!is_file_handle_expression(statement.file_handle)) {
            report(
                "FSIM-ELAB-SVFILE-001",
                "$fclose handle must be a 32-bit integer expression",
                statement.file_handle.span);
            break;
        }
        auto handle = lower_expression(statement.file_handle, 32);
        if (!handle) {
            break;
        }
        if (register_width(*handle) != 32) {
            *handle = resize_register(
                *handle,
                32,
                is_signed_expression(statement.file_handle));
        }
        process_.operations.emplace_back(FileClose { *handle });
        break;
    }
    case StatementKind::FileFlush: {
        if (statement.file_handle.kind == ExpressionKind::Invalid) {
            process_.operations.emplace_back(FileFlush { 0, true });
            break;
        }
        if (!is_file_handle_expression(statement.file_handle)) {
            report(
                "FSIM-ELAB-SVFILE-016",
                "$fflush handle must be a 32-bit integer expression",
                statement.file_handle.span);
            break;
        }
        auto handle = lower_expression(statement.file_handle, 32);
        if (!handle)
            break;
        if (register_width(*handle) != 32) {
            *handle = resize_register(
                *handle, 32,
                is_signed_expression(statement.file_handle));
        }
        process_.operations.emplace_back(FileFlush { *handle, false });
        break;
    }
    case StatementKind::FileDisplay: {
        if (!is_file_handle_expression(statement.file_handle)) {
            report(
                "FSIM-ELAB-SVFILE-001",
                "file output handle must be a 32-bit integer "
                "expression",
                statement.file_handle.span);
            break;
        }
        auto handle = lower_expression(statement.file_handle, 32);
        if (!handle) {
            break;
        }
        if (register_width(*handle) != 32) {
            *handle = resize_register(
                *handle,
                32,
                is_signed_expression(statement.file_handle));
        }
        const auto scalar_kind_of =
            [&](const Expression& value) {
                if (value.text == "$time") {
                    return frontend::SystemVerilogScalarKind::Time;
                }
                if (value.text == "$realtime") {
                    return frontend::SystemVerilogScalarKind::Realtime;
                }
                const auto* output_type = value.kind
                        == frontend::ExpressionKind::Identifier
                    ? object_type(value.text)
                    : nullptr;
                return output_type != nullptr
                    ? output_type->systemverilog_scalar
                    : value.systemverilog_scalar_kind;
            };
        const auto scalar_format_matches =
            [&](const Expression& value,
                const frontend::OutputFormat output_format) {
                const auto scalar_kind = scalar_kind_of(value);
                const bool real_kind = scalar_kind
                        == frontend::SystemVerilogScalarKind::ShortReal
                    || scalar_kind
                        == frontend::SystemVerilogScalarKind::Real
                    || scalar_kind
                        == frontend::SystemVerilogScalarKind::Realtime;
                const bool real_format = output_format
                        == frontend::OutputFormat::RealScientific
                    || output_format
                        == frontend::OutputFormat::RealFixed
                    || output_format
                        == frontend::OutputFormat::RealGeneral;
                return real_kind ? real_format
                    : scalar_kind
                        == frontend::SystemVerilogScalarKind::Time
                    ? output_format
                            == frontend::OutputFormat::Time
                        || output_format
                            == frontend::OutputFormat::Decimal
                    : scalar_kind
                        == frontend::SystemVerilogScalarKind::Chandle
                    ? output_format
                        == frontend::OutputFormat::Hexadecimal
                    : !real_format
                        && output_format
                            != frontend::OutputFormat::Time;
            };
        if (statement.output_postponed) {
            MonitorInstall monitor;
            monitor.file_handle = *handle;
            monitor.newline = statement.output_newline;
            monitor.one_shot = !statement.output_monitor;
            const auto task_name = statement.output_monitor
                ? "$fmonitor"
                : "$fstrobe";
            if (statement.output_values.empty()
                && !statement.output_format) {
                monitor.trailing_text = statement.output_text;
                process_.operations.emplace_back(std::move(monitor));
                break;
            }
            std::string pending_prefix;
            const auto append_value
                = [&](const frontend::OutputValue& output) {
                      auto prefix = std::move(pending_prefix)
                          + output.prefix;
                      if (output.format
                          == frontend::OutputFormat::Hierarchy) {
                          pending_prefix = std::move(prefix) + hierarchy_;
                          return;
                      }
                      MonitorValue value;
                      value.prefix = std::move(prefix);
                      value.minimum_width = output.minimum_width;
                      value.left_justify = output.left_justify;
                      value.zero_pad = output.zero_pad;
                      value.suppress_leading_zero
                          = output.suppress_leading_zero;
                      if (output.format
                          == frontend::OutputFormat::Time) {
                          value.kind = MonitorValueKind::time;
                          value.use_timeformat_width
                              = output.minimum_width == 0
                              && !output.suppress_leading_zero;
                      } else {
                          if (!scalar_format_matches(
                                  output.value, output.format)) {
                              report(
                                  "FSIM-ELAB-SVFILE-007",
                                  std::string { task_name }
                                      + " conversion is incompatible with the value type",
                                  output.value.span);
                              return;
                          }
                          if (output.value.kind
                              != frontend::ExpressionKind::Identifier) {
                              report(
                                  "FSIM-ELAB-SVFILE-007",
                                  std::string { task_name }
                                      + " requires direct packed-signal value expressions",
                                  output.value.span);
                              return;
                          }
                          const auto signal = signals_.find(
                              output.value.text);
                          if (signal == signals_.end()) {
                              report(
                                  "FSIM-ELAB-020",
                                  "unknown file-strobe signal '"
                                      + output.value.text + "'",
                                  output.value.span);
                              return;
                          }
                          value.kind = MonitorValueKind::signal;
                          value.signal = signal->second;
                          value.format = runtime_output_format(
                              output.format);
                          value.scalar_kind = scalar_kind_of(
                              output.value);
                          value.signed_decimal
                              = value.format
                                  == runtime::simir::OutputFormat::decimal
                              && is_signed_expression(output.value);
                      }
                      monitor.values.push_back(std::move(value));
                  };
            if (!statement.output_values.empty()) {
                for (const auto& output : statement.output_values) {
                    append_value(output);
                }
                monitor.trailing_text = std::move(pending_prefix)
                    + statement.output_trailing_text;
            } else {
                append_value(
                    frontend::OutputValue {
                        statement.value,
                        *statement.output_format,
                        statement.output_prefix,
                        statement.output_suppress_leading_zero,
                        statement.output_minimum_width,
                        statement.output_left_justify,
                        statement.output_zero_pad });
                monitor.trailing_text = std::move(pending_prefix)
                    + statement.output_suffix;
            }
            if (!monitor.values.empty()
                || !monitor.trailing_text.empty()) {
                process_.operations.emplace_back(std::move(monitor));
            }
            break;
        }
        const auto lower_output =
            [&](const frontend::OutputValue& output,
                const std::string& suffix,
                const bool newline) {
                if (output.format
                    == frontend::OutputFormat::Hierarchy) {
                    process_.operations.emplace_back(
                        FileWriteLiteral {
                            *handle,
                            output.prefix + hierarchy_ + suffix,
                            newline });
                    return;
                }
                if ((output.format
                            == frontend::OutputFormat::String
                        || output.format
                            == frontend::OutputFormat::Decimal)
                    && is_string_expression(output.value)) {
                    const auto source = lower_string_expression(output.value);
                    if (source) {
                        process_.operations.emplace_back(
                            FileWriteString {
                                *handle,
                                *source,
                                output.prefix,
                                suffix,
                                newline });
                    }
                    return;
                }
                if (!scalar_format_matches(
                        output.value, output.format)) {
                    report(
                        "FSIM-ELAB-SVFILE-007",
                        "formatted file conversion is incompatible with the value type",
                        output.value.span);
                    return;
                }
                const auto width = infer_width(output.value).value_or(std::size_t { 32 });
                const auto source = lower_expression(output.value, width);
                if (!source) {
                    report(
                        "FSIM-ELAB-SVFILE-007",
                        "formatted file output value cannot be lowered",
                        output.value.span);
                    return;
                }
                const auto format = runtime_output_format(output.format);
                process_.operations.emplace_back(
                    FileWriteFormatted {
                        *handle,
                        *source,
                        static_cast<std::uint32_t>(width),
                        format,
                        output.prefix,
                        suffix,
                        newline,
                        format
                                == runtime::simir::OutputFormat::decimal
                            && is_signed_expression(output.value),
                        output.suppress_leading_zero,
                        output.minimum_width,
                        output.left_justify,
                        output.zero_pad,
                        scalar_kind_of(output.value) });
            };
        if (!statement.output_values.empty()) {
            for (std::size_t index = 0;
                index < statement.output_values.size();
                ++index) {
                const bool last = index + 1 == statement.output_values.size();
                lower_output(
                    statement.output_values[index],
                    last ? statement.output_trailing_text
                         : std::string { },
                    last && statement.output_newline);
            }
            break;
        }
        if (!statement.output_format) {
            process_.operations.emplace_back(
                FileWriteLiteral {
                    *handle,
                    statement.output_text,
                    statement.output_newline });
            break;
        }
        lower_output(
            frontend::OutputValue {
                statement.value,
                *statement.output_format,
                statement.output_prefix,
                statement.output_suppress_leading_zero,
                statement.output_minimum_width,
                statement.output_left_justify,
                statement.output_zero_pad },
            statement.output_suffix,
            statement.output_newline);
        break;
    }
    case StatementKind::MemoryLoad: {
        const auto path = lower_string_expression(statement.value);
        if (!path) {
            report(
                "FSIM-ELAB-SVMEMORY-002",
                "memory-file name must be a string expression",
                statement.value.span);
            break;
        }
        if (statement.target.kind
            != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-SVMEMORY-003",
                "memory-file operand must be a direct static-array "
                "object",
                statement.target.span);
            break;
        }
        if (!statement.memory_write
            && read_only_container_objects_.contains(
                statement.target.text)) {
            report(
                "FSIM-ELAB-SVPORT-009",
                "an input container port is read-only within its "
                "module",
                statement.target.span);
            break;
        }
        const auto target = lower_container_expression(
            statement.target);
        const auto* source_type = object_type(statement.target.text);
        const auto runtime_type = source_type
            ? container_type(
                  *source_type,
                  statement.target.span)
            : std::nullopt;
        if (!target || !runtime_type) {
            break;
        }
        if (!runtime_type->fixed) {
            report(
                "FSIM-ELAB-SVMEMORY-003",
                "$readmem*/$writemem* requires a bounded "
                "static unpacked array",
                statement.target.span);
            break;
        }
        const auto lower_bound =
            [&](const Expression* expression)
            -> std::optional<RegisterId> {
            if (!expression) {
                return std::nullopt;
            }
            auto result = lower_expression(*expression, 32);
            if (!result) {
                return std::nullopt;
            }
            if (register_width(*result) != 32) {
                *result = resize_register(
                    *result, 32,
                    is_signed_expression(*expression));
            }
            return result;
        };
        const auto* start_expression = statement.task_arguments.empty()
            ? nullptr
            : &statement.task_arguments[0];
        const auto* finish_expression = statement.task_arguments.size() < 2
            ? nullptr
            : &statement.task_arguments[1];
        const auto start = lower_bound(start_expression);
        const auto finish = lower_bound(finish_expression);
        if ((start_expression && !start)
            || (finish_expression && !finish)) {
            report(
                "FSIM-ELAB-SVMEMORY-004",
                "memory-file start and finish must be integral "
                "expressions",
                statement.span);
            break;
        }
        process_.operations.emplace_back(
            LoadMemory {
                *target, *path, start, finish,
                statement.memory_hex,
                statement.memory_write });
        if (!statement.memory_write) {
            if (const auto object = container_objects_.find(
                    statement.target.text);
                object != container_objects_.end()) {
                process_.operations.emplace_back(
                    WriteContainerObject {
                        object->second, *target, std::nullopt });
            }
        }
        break;
    }
    case StatementKind::Display: {
        const auto scalar_kind_of = [&](const Expression& value) {
            if (value.text == "$time") {
                return frontend::SystemVerilogScalarKind::Time;
            }
            if (value.text == "$realtime") {
                return frontend::SystemVerilogScalarKind::Realtime;
            }
            const auto* type = value.kind == ExpressionKind::Identifier
                ? object_type(value.text)
                : nullptr;
            return type != nullptr
                ? type->systemverilog_scalar
                : value.systemverilog_scalar_kind;
        };
        const auto scalar_format_matches = [&](
                                               const Expression& value,
                                               const frontend::OutputFormat format) {
            const auto scalar = scalar_kind_of(value);
            const bool real_scalar = scalar == frontend::SystemVerilogScalarKind::ShortReal
                || scalar == frontend::SystemVerilogScalarKind::Real
                || scalar == frontend::SystemVerilogScalarKind::Realtime;
            const bool real_format = format == frontend::OutputFormat::RealScientific
                || format == frontend::OutputFormat::RealFixed
                || format == frontend::OutputFormat::RealGeneral;
            return real_scalar ? real_format
                : scalar == frontend::SystemVerilogScalarKind::Time
                ? format == frontend::OutputFormat::Decimal
                    || format == frontend::OutputFormat::Time
                : scalar == frontend::SystemVerilogScalarKind::Chandle
                ? format == frontend::OutputFormat::Hexadecimal
                : !real_format;
        };
        if (statement.output_monitor
            || statement.output_postponed) {
            if (statement.output_values.empty()
                && !statement.output_format) {
                process_.operations.emplace_back(
                    MonitorInstall {
                        { },
                        statement.output_text,
                        statement.output_newline,
                        statement.output_postponed
                            && !statement.output_monitor,
                        std::nullopt });
                break;
            }
            MonitorInstall monitor;
            monitor.newline = statement.output_newline;
            monitor.one_shot = statement.output_postponed
                && !statement.output_monitor;
            std::string pending_prefix;
            const auto append_value =
                [&](const frontend::OutputValue& output) {
                    auto prefix = std::move(pending_prefix) + output.prefix;
                    if (output.format
                        == frontend::OutputFormat::Hierarchy) {
                        pending_prefix = std::move(prefix) + hierarchy_;
                        return;
                    }
                    MonitorValue value;
                    value.prefix = std::move(prefix);
                    value.minimum_width = output.minimum_width;
                    value.left_justify = output.left_justify;
                    value.zero_pad = output.zero_pad;
                    value.suppress_leading_zero = output.suppress_leading_zero;
                    if (output.format
                        == frontend::OutputFormat::Time) {
                        value.kind = MonitorValueKind::time;
                        value.use_timeformat_width = output.minimum_width == 0
                            && !output.suppress_leading_zero;
                    } else {
                        if (!scalar_format_matches(
                                output.value, output.format)) {
                            report(
                                "FSIM-ELAB-102",
                                "formatted output conversion is incompatible with the value type",
                                output.value.span);
                            return;
                        }
                        if (output.value.kind
                            != frontend::ExpressionKind::Identifier) {
                            report(
                                monitor.one_shot
                                    ? "FSIM-ELAB-108"
                                    : "FSIM-ELAB-103",
                                monitor.one_shot
                                    ? "$strobe currently requires direct "
                                      "packed-signal value expressions"
                                    : "$monitor currently requires direct "
                                      "packed-signal value expressions",
                                output.value.span);
                            return;
                        }
                        const auto signal = signals_.find(output.value.text);
                        if (signal == signals_.end()) {
                            report(
                                "FSIM-ELAB-020",
                                "unknown monitor signal '"
                                    + output.value.text + "'",
                                output.value.span);
                            return;
                        }
                        value.kind = MonitorValueKind::signal;
                        value.signal = signal->second;
                        value.format = runtime_output_format(output.format);
                        value.scalar_kind = scalar_kind_of(output.value);
                        value.signed_decimal = value.format
                                == runtime::simir::OutputFormat::
                                    decimal
                            && is_signed_expression(output.value);
                    }
                    monitor.values.push_back(std::move(value));
                };
            if (!statement.output_values.empty()) {
                for (const auto& output :
                    statement.output_values) {
                    append_value(output);
                }
                monitor.trailing_text = std::move(pending_prefix)
                    + statement.output_trailing_text;
            } else {
                append_value(
                    frontend::OutputValue {
                        statement.value,
                        *statement.output_format,
                        statement.output_prefix,
                        statement.output_suppress_leading_zero,
                        statement.output_minimum_width,
                        statement.output_left_justify,
                        statement.output_zero_pad });
                monitor.trailing_text = std::move(pending_prefix)
                    + statement.output_suffix;
            }
            if (!monitor.values.empty()) {
                process_.operations.emplace_back(
                    std::move(monitor));
            }
            break;
        }
        if (!statement.output_values.empty()) {
            for (std::size_t index = 0;
                index < statement.output_values.size();
                ++index) {
                const auto& output = statement.output_values[index];
                const bool last = index + 1 == statement.output_values.size();
                if (output.format
                    == frontend::OutputFormat::Hierarchy) {
                    process_.operations.emplace_back(
                        Display {
                            output.prefix + hierarchy_
                                + (last
                                        ? statement.output_trailing_text
                                        : std::string { }),
                            last && statement.output_newline,
                            statement.output_postponed });
                    continue;
                }
                if (output.format == frontend::OutputFormat::Time
                    && (!output.value.valid()
                        || output.value.text == "$time")) {
                    process_.operations.emplace_back(
                        TimeDisplay {
                            output.prefix,
                            last
                                ? statement.output_trailing_text
                                : std::string { },
                            last && statement.output_newline,
                            statement.output_postponed,
                            output.minimum_width,
                            output.left_justify,
                            output.zero_pad,
                            output.minimum_width == 0
                                && !output.suppress_leading_zero });
                    continue;
                }
                if (output.format
                        == frontend::OutputFormat::String
                    && is_string_expression(output.value)) {
                    const auto source = lower_string_expression(output.value);
                    if (!source) {
                        continue;
                    }
                    process_.operations.emplace_back(
                        StringDisplay {
                            *source,
                            output.prefix,
                            last
                                ? statement.output_trailing_text
                                : std::string { },
                            last && statement.output_newline,
                            statement.output_postponed });
                    continue;
                }
                if (!scalar_format_matches(
                        output.value, output.format)) {
                    report(
                        "FSIM-ELAB-102",
                        "formatted output conversion is incompatible with the value type",
                        output.value.span);
                    continue;
                }
                const auto width = infer_width(output.value)
                                       .value_or(std::size_t { 32 });
                const auto source = lower_expression(output.value, width);
                if (!source) {
                    report(
                        "FSIM-ELAB-102",
                        "formatted output value cannot be lowered",
                        output.value.span);
                    continue;
                }
                const auto format = runtime_output_format(output.format);
                process_.operations.emplace_back(
                    FormatDisplay {
                        *source,
                        format,
                        output.prefix,
                        last
                            ? statement.output_trailing_text
                            : std::string { },
                        last && statement.output_newline,
                        statement.output_postponed,
                        format
                                == runtime::simir::OutputFormat::decimal
                            && is_signed_expression(output.value),
                        output.suppress_leading_zero,
                        output.minimum_width,
                        output.left_justify,
                        output.zero_pad,
                        scalar_kind_of(output.value) });
            }
            break;
        }
        if (statement.output_format) {
            if (*statement.output_format
                    == frontend::OutputFormat::String
                && is_string_expression(statement.value)) {
                const auto source = lower_string_expression(statement.value);
                if (source) {
                    process_.operations.emplace_back(
                        StringDisplay {
                            *source,
                            statement.output_prefix,
                            statement.output_suffix,
                            statement.output_newline,
                            statement.output_postponed });
                }
                break;
            }
            if (!scalar_format_matches(
                    statement.value, *statement.output_format)) {
                report(
                    "FSIM-ELAB-102",
                    "formatted output conversion is incompatible with the value type",
                    statement.value.span);
                break;
            }
            const auto width = infer_width(statement.value)
                                   .value_or(std::size_t { 32 });
            const auto source = lower_expression(statement.value, width);
            if (!source) {
                report(
                    "FSIM-ELAB-102",
                    "formatted output value cannot be lowered",
                    statement.span);
                break;
            }
            const auto format = runtime_output_format(*statement.output_format);
            process_.operations.emplace_back(
                FormatDisplay {
                    *source,
                    format,
                    statement.output_prefix,
                    statement.output_suffix,
                    statement.output_newline,
                    statement.output_postponed,
                    format == runtime::simir::OutputFormat::decimal
                        && is_signed_expression(statement.value),
                    statement.output_suppress_leading_zero,
                    statement.output_minimum_width,
                    statement.output_left_justify,
                    statement.output_zero_pad,
                    scalar_kind_of(statement.value) });
        } else {
            process_.operations.emplace_back(
                Display {
                    statement.output_text,
                    statement.output_newline,
                    statement.output_postponed });
        }
        break;
    }
    case StatementKind::Report: {
        lower_assert(statement);
        break;
    }
    case StatementKind::Pause:
        process_.operations.emplace_back(Pause { });
        break;
    case StatementKind::Finish:
        process_.operations.emplace_back(Stop { });
        break;
    case StatementKind::Exit:
        if (!systemverilog_program_owner_) {
            report(
                "FSIM-ELAB-SVEXIT-001",
                "$exit is legal only in a SystemVerilog program",
                statement.span);
        }
        process_.operations.emplace_back(Halt { true });
        break;
    case StatementKind::Fork:
        lower_fork(statement);
        break;
    case StatementKind::WaitFork:
        process_.operations.emplace_back(WaitFork { });
        break;
    case StatementKind::DisableFork:
        process_.operations.emplace_back(DisableFork { });
        break;
    case StatementKind::Disable: {
        const auto found = std::find_if(
            block_controls_.rbegin(), block_controls_.rend(),
            [&](const BlockControlContext& control) {
                return control.label == statement.task_name;
            });
        if (found != block_controls_.rend() && found->fork_site) {
            process_.operations.emplace_back(
                DisableFork { found->fork_site });
            break;
        }
        if (found != block_controls_.rend()) {
            found->disable_jumps.push_back(
                static_cast<InstructionIndex>(
                    process_.operations.size()));
            process_.operations.emplace_back(Jump { });
            break;
        }
        const auto named_fork = std::find_if(
            named_fork_controls_.rbegin(), named_fork_controls_.rend(),
            [&](const NamedForkControl& control) {
                return control.label == statement.task_name
                    && control.scope.size() <= local_scope_.size()
                    && std::equal(
                        control.scope.begin(), control.scope.end(),
                        local_scope_.begin());
            });
        if (named_fork != named_fork_controls_.rend()) {
            process_.operations.emplace_back(
                DisableFork { named_fork->site });
            break;
        }
        const auto named_block = std::find_if(
            named_block_controls_.rbegin(), named_block_controls_.rend(),
            [&](const NamedBlockControl& control) {
                return control.label == statement.task_name
                    && control.scope.size() <= local_scope_.size()
                    && std::equal(
                        control.scope.begin(), control.scope.end(),
                        local_scope_.begin());
            });
        if (named_block != named_block_controls_.rend()) {
            const auto operation = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(DisableBlock {
                named_block->begin,
                named_block->end.value_or(0) });
            if (!named_block->end) {
                named_block->disable_operations.push_back(operation);
            }
            break;
        }
        report(
            "FSIM-ELAB-SVDISABLE-001",
            "disable target '" + statement.task_name
                + "' is not visible as a named block",
            statement.span);
        break;
    }
    case StatementKind::Block:
        lower_block(statement);
        break;
    case StatementKind::Null:
        break;
    }
    if (!statement_scope.empty()) {
        local_scope_.pop_back();
    }
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
            static_cast<std::uint32_t>(span.begin.column) },
        debug_scope_name() });
}

void Lowerer::lower_wait_until(const Statement& statement)
{
    if (language_ != frontend::Language::Vhdl2008) {
        lower_immediate_condition_wait(statement);
        return;
    }

    const auto dependency_start = implicit_signal_dependencies_.size();
    const auto condition_operations_start = process_.operations.size();
    const auto condition = lower_condition(
        statement.condition,
        "FSIM-ELAB-079",
        "wait-until");
    if (!condition) {
        return;
    }
    std::vector<runtime::simir::Operation> condition_operations(
        std::make_move_iterator(
            process_.operations.begin()
            + static_cast<std::ptrdiff_t>(
                condition_operations_start)),
        std::make_move_iterator(
            process_.operations.end()));
    process_.operations.resize(condition_operations_start);

    std::vector<SignalId> waited_signals;
    std::vector<runtime::simir::EdgeKind> waited_edges;
    if (!statement.sensitivities.empty()) {
        auto resolved = resolve_wait_sensitivities(statement);
        waited_signals = std::move(resolved.first);
        waited_edges = std::move(resolved.second);
    } else {
        std::set<std::string> dependencies;
        collect_identifiers(
            statement.condition, dependencies);
        for (const auto& dependency : dependencies) {
            if (locals_.contains(dependency)) {
                continue;
            }
            if (const auto found = signals_.find(dependency);
                found != signals_.end()) {
                waited_signals.push_back(found->second);
            }
        }
        for (auto index = dependency_start;
            index < implicit_signal_dependencies_.size();
            ++index) {
            waited_signals.push_back(
                implicit_signal_dependencies_[index]);
        }
        std::ranges::sort(waited_signals);
        waited_signals.erase(
            std::unique(
                waited_signals.begin(),
                waited_signals.end()),
            waited_signals.end());
    }

    if (waited_signals.empty() && !statement.delay) {
        process_.operations.emplace_back(WaitForever { });
        lower_statements(statement.statements);
        return;
    }

    std::optional<RegisterId> timed_out;
    if (statement.delay) {
        timed_out = allocate_register(
            1, frontend::ValueDomain::Boolean);
    }

    const auto initial_wait = static_cast<InstructionIndex>(
        process_.operations.size());
    WaitOn first_wait {
        waited_signals, waited_edges
    };
    if (statement.delay) {
        first_wait.timeout = statement.delay->magnitude;
        first_wait.timeout_result = timed_out;
    }
    process_.operations.emplace_back(
        std::move(first_wait));

    std::optional<InstructionIndex> timeout_branch;
    if (timed_out) {
        timeout_branch = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(
            Branch {
                *timed_out,
                0,
                0,
                UnknownBranchPolicy::error });
    }

    const auto condition_start = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.insert(
        process_.operations.end(),
        std::make_move_iterator(
            condition_operations.begin()),
        std::make_move_iterator(
            condition_operations.end()));
    const auto condition_branch = static_cast<InstructionIndex>(
        process_.operations.size());
    const auto unknown_policy = language_ == frontend::Language::Vhdl2008
        ? UnknownBranchPolicy::error
        : UnknownBranchPolicy::when_false;
    process_.operations.emplace_back(
        Branch { *condition, 0, 0, unknown_policy });

    const auto rewait_start = static_cast<InstructionIndex>(
        process_.operations.size());
    WaitOn rewait {
        std::move(waited_signals),
        std::move(waited_edges)
    };
    if (statement.delay) {
        rewait.timeout = statement.delay->magnitude;
        rewait.timeout_result = timed_out;
        rewait.timeout_origin = initial_wait;
    }
    process_.operations.emplace_back(std::move(rewait));
    process_.operations.emplace_back(
        Jump {
            timeout_branch.value_or(
                condition_start) });

    const auto satisfied_start = static_cast<InstructionIndex>(
        process_.operations.size());
    lower_statements(statement.statements);
    process_.operations[condition_branch] = Branch {
        *condition,
        satisfied_start,
        rewait_start,
        unknown_policy
    };
    if (timeout_branch) {
        process_.operations[*timeout_branch] = Branch {
            *timed_out,
            satisfied_start,
            condition_start,
            UnknownBranchPolicy::error
        };
    }
}

void Lowerer::lower_immediate_condition_wait(
    const Statement& statement)
{
    const auto condition_start = static_cast<InstructionIndex>(
        process_.operations.size());
    const auto condition = lower_condition(
        statement.condition,
        "FSIM-ELAB-079",
        "wait");
    if (!condition) {
        return;
    }
    const auto branch_index = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(
        Branch {
            *condition,
            0,
            0,
            UnknownBranchPolicy::when_false });

    const auto wait_start = static_cast<InstructionIndex>(
        process_.operations.size());
    std::set<std::string> dependencies;
    collect_identifiers(
        statement.condition, dependencies);
    std::vector<SignalId> waited_signals;
    for (const auto& dependency : dependencies) {
        if (locals_.contains(dependency)) {
            continue;
        }
        if (const auto found = signals_.find(dependency);
            found != signals_.end()) {
            waited_signals.push_back(found->second);
        }
    }
    std::ranges::sort(waited_signals);
    waited_signals.erase(
        std::unique(
            waited_signals.begin(),
            waited_signals.end()),
        waited_signals.end());
    if (waited_signals.empty()) {
        process_.operations.emplace_back(WaitForever { });
    } else {
        process_.operations.emplace_back(
            WaitOn { std::move(waited_signals) });
        process_.operations.emplace_back(
            Jump { condition_start });
    }

    const auto satisfied_start = static_cast<InstructionIndex>(
        process_.operations.size());
    lower_statements(statement.statements);
    process_.operations[branch_index] = Branch {
        *condition,
        satisfied_start,
        wait_start,
        UnknownBranchPolicy::when_false
    };
}

} // namespace fsim::elaboration
