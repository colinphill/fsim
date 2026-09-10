// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"
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

void Lowerer::set_systemverilog_interface_literals(
    std::unordered_map<std::string,
        SystemVerilogInterfaceLiteral> literals)
{
    systemverilog_interface_literals_ = std::move(literals);
}

void Lowerer::set_systemverilog_interface_type_identities(
    std::unordered_map<const frontend::Type*,
        std::vector<std::pair<std::string, std::string>>>
        identities)
{
    systemverilog_interface_type_identities_ =
        std::move(identities);
}


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
    sample_concurrent_assertion_reads_
        = source.systemverilog_concurrent_assertion;
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
    if (const auto literal_name
        = systemverilog_interface_literal_name(expression)) {
        if (const auto literal =
                systemverilog_interface_literals_.find(*literal_name);
            literal != systemverilog_interface_literals_.end()) {
            return &literal->second.type;
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

std::optional<std::string>
Lowerer::systemverilog_interface_literal_name(
    const Expression& expression)
{
    if (expression.kind == ExpressionKind::Identifier) {
        return expression.text;
    }
    if (expression.kind != ExpressionKind::Index
        || expression.operands.size() != 2U
        || expression.operands[0].kind
            != ExpressionKind::Identifier
        || expression.operands[1].kind
            != ExpressionKind::IntegerLiteral) {
        return std::nullopt;
    }
    return expression.operands[0].text + "["
        + expression.operands[1].text + "]";
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

} // namespace fsim::elaboration
