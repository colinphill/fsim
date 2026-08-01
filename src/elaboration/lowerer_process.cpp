// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

[[nodiscard]] runtime::simir::OutputFormat runtime_output_format(
    const frontend::OutputFormat source) {
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
  case frontend::OutputFormat::Hierarchy:
  case frontend::OutputFormat::Time:
    break;
  }
  throw std::logic_error{"invalid frontend output format"};
}

}  // namespace

Lowerer::Lowerer(
        ElaboratedDesign& design,
        const std::unordered_map<std::string, SignalId>& signals,
        const std::unordered_map<std::string, StringObjectId>&
            string_objects,
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
        std::vector<Diagnostic>& diagnostics)
        : design_(design),
          signals_(signals),
          string_objects_(string_objects),
          container_objects_(container_objects),
          read_only_container_objects_(
              read_only_container_objects),
          visible_types_(visible_types),
          visible_type_marks_(visible_type_marks),
          functions_(functions),
          tasks_(tasks),
          procedures_(procedures),
          diagnostics_(diagnostics) {}



    Process Lowerer::lower_process(
        const frontend::Process& source,
        const frontend::Language language,
        const std::string_view hierarchy) {
        process_ = Process{};
        language_ = language;
        process_kind_ = source.kind;
        hierarchy_ = std::string{hierarchy};
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
        process_.name = std::string(hierarchy) + "."
            + (source.name.empty()
                   ? "process_" + std::to_string(process_.id)
                   : source.name);
        process_.final = source.kind == ProcessKind::Final;
        if (process_.final) {
            process_.initialize = false;
        }
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
                            {found->second,
                             runtime::simir::EdgeKind::any});
                    }
                }
                if (process_.static_sensitivity.empty()) {
                    report(
                        "FSIM-ELAB-SVEVENT-001",
                        "packed process event expression has no readable "
                        "signal dependencies",
                        sensitivity.span);
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
            process_.static_sensitivity.push_back({found->second, edge});
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
                        {found->second,
                         runtime::simir::EdgeKind::any});
                }
            }
            if (process_.static_sensitivity.empty()) {
                report(
                    "FSIM-ELAB-061",
                    "wildcard process sensitivity has no readable signal "
                    "dependencies",
                    source.span);
            }
        }

        const bool verilog_event_process =
            language != frontend::Language::Vhdl2008
            && source.kind != ProcessKind::Initial
            && source.kind
                != ProcessKind::SystemVerilogAlwaysComb
            && source.kind
                != ProcessKind::SystemVerilogAlwaysLatch
            && !process_.static_sensitivity.empty();
        const bool body_timed_always =
            language != frontend::Language::Vhdl2008
            && source.kind == ProcessKind::VerilogAlways
            && source.sensitivities.empty();
        const Statement* vhdl_edge_guard =
            language == frontend::Language::Vhdl2008
                ? recognized_vhdl_edge_guard(source)
                : nullptr;
        const bool waits_before_first_execution =
            verilog_event_process || vhdl_edge_guard != nullptr;
        const bool vhdl_explicit_wait =
            language == frontend::Language::Vhdl2008
            && contains_explicit_wait(source.statements);
        const auto resume_entry =
            static_cast<InstructionIndex>(process_.operations.size());
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
                process_.operations.emplace_back(WaitSensitivity{});
                const auto current = lower_expression(
                    general_sensitivity->expression, *width);
                if (current) {
                    const auto equal = allocate_register(
                        1, frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(Binary{
                        BinaryOperator::case_equal,
                        equal,
                        *baseline,
                        *current});
                    process_.operations.emplace_back(
                        CopyRegister{*baseline, *current});
                    event_filter_branch =
                        static_cast<InstructionIndex>(
                            process_.operations.size());
                    process_.operations.emplace_back(Branch{
                        equal, 0, 0,
                        UnknownBranchPolicy::when_false});
                }
            }
        } else if (waits_before_first_execution) {
            process_.operations.emplace_back(WaitSensitivity{});
        }
        const auto body_entry = static_cast<InstructionIndex>(
            process_.operations.size());
        if (event_filter_branch && event_wait_entry) {
            process_.operations[*event_filter_branch] = Branch{
                std::get<Branch>(
                    process_.operations[*event_filter_branch])
                    .condition,
                *event_wait_entry,
                body_entry,
                UnknownBranchPolicy::when_false};
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
        if (source.kind == ProcessKind::Initial
            || source.kind == ProcessKind::Final) {
            process_.operations.emplace_back(Halt{});
        } else if (waits_before_first_execution) {
            // Event-controlled processes wait before their first execution.
            // Returning directly to operation zero preserves one body
            // execution per matching event.
            process_.operations.emplace_back(Jump{
                event_wait_entry.value_or(resume_entry)});
        } else if (body_timed_always) {
            process_.operations.emplace_back(Jump{resume_entry});
        } else if (
            language == frontend::Language::Vhdl2008
            && source.sensitivities.empty()
            && vhdl_explicit_wait) {
            // A VHDL process implicitly repeats. Explicit waits inside the
            // body provide the required suspension boundary.
            process_.operations.emplace_back(Jump{resume_entry});
        } else {
            // A VHDL process with a sensitivity list executes once at time
            // zero, then waits at the implicit trailing sensitivity point.
            process_.operations.emplace_back(WaitSensitivity{});
            process_.operations.emplace_back(Jump{resume_entry});
        }
        lower_pending_tasks();
        lower_pending_procedures();
        lower_pending_functions();
        process_.register_count = next_register_;
        process_.string_register_count = next_string_register_;
        process_.container_register_count = next_container_register_;
        process_.register_value_kinds.reserve(register_domains_.size());
        for (const auto domain : register_domains_) {
            process_.register_value_kinds.push_back(
                value_kind(domain));
        }
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



    Process Lowerer::lower_concurrent(
        const Statement& statement,
        const frontend::Language language,
        const std::string& name,
        const std::size_t order) {
        process_ = Process{};
        language_ = language;
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
        process_.name = name + "."
            + (statement.label.empty()
                   ? "concurrent_" + std::to_string(order)
                   : statement.label);
        initialize_function_support();
        initialize_task_support();
        initialize_procedure_support();
        emit_debug_point(
            DebugPointKind::process_entry, statement.span);

        std::set<std::string> dependencies;
        collect_wildcard_identifiers(
            std::vector<Statement>{statement}, dependencies);
        for (const auto& dependency : dependencies) {
            if (const auto found = signals_.find(dependency); found != signals_.end()) {
                process_.static_sensitivity.push_back({found->second, runtime::simir::EdgeKind::any});
            }
        }
        lower_statement(statement);
        if (!process_.static_sensitivity.empty()) {
            process_.operations.emplace_back(WaitSensitivity{});
            process_.operations.emplace_back(Jump{0});
        } else {
            process_.operations.emplace_back(Halt{});
        }
        lower_pending_tasks();
        lower_pending_procedures();
        lower_pending_functions();
        process_.register_count = next_register_;
        process_.string_register_count = next_string_register_;
        process_.container_register_count = next_container_register_;
        process_.register_value_kinds.reserve(register_domains_.size());
        for (const auto domain : register_domains_) {
            process_.register_value_kinds.push_back(
                value_kind(domain));
        }
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

[[nodiscard]] const frontend::Type* Lowerer::visible_type(
        const std::string_view name) const {
        const auto found = visible_types_.find(std::string{name});
        return found == visible_types_.end()
            ? nullptr
            : found->second;
    }



    [[nodiscard]] const frontend::Type* Lowerer::object_type(
        const std::string_view name) const {
        if (const auto local =
                local_types_.find(std::string{name});
            local != local_types_.end()) {
            return local->second;
        }
        return visible_type(name);
    }



    [[nodiscard]] const frontend::Type* Lowerer::visible_type_mark(
        const std::string_view name) const {
        const auto found =
            visible_type_marks_.find(std::string{name});
        return found == visible_type_marks_.end()
            ? nullptr
            : found->second;
    }



    [[nodiscard]] const frontend::Type*
    Lowerer::enumeration_expression_type(
        const Expression& expression) const {
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
        const bool enumeration_result =
            expression.text == "'left"
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



    [[nodiscard]] std::pair<std::int32_t, std::int32_t>
    Lowerer::integer_bounds(
        const std::optional<frontend::IntegerRange>& range) {
        if (!range) {
            return {
                std::numeric_limits<std::int32_t>::min(),
                std::numeric_limits<std::int32_t>::max()};
        }
        return {
            static_cast<std::int32_t>(
                std::min(range->left, range->right)),
            static_cast<std::int32_t>(
                std::max(range->left, range->right))};
    }



    void Lowerer::emit_integer_check(
        const RegisterId source,
        const std::optional<frontend::IntegerRange>& range) {
        const auto [lower, upper] = integer_bounds(range);
        process_.operations.emplace_back(
            IntegerCheck{source, lower, upper});
    }



    [[nodiscard]] std::pair<std::int32_t, std::int32_t>
    Lowerer::enumeration_bounds(const frontend::Type& type) {
        if (type.enumeration_range) {
            return {
                static_cast<std::int32_t>(
                    std::min(
                        type.enumeration_range->left,
                        type.enumeration_range->right)),
                static_cast<std::int32_t>(
                    std::max(
                        type.enumeration_range->left,
                        type.enumeration_range->right))};
        }
        return {
            0,
            static_cast<std::int32_t>(
                type.enumeration_literals.size() - 1U)};
    }



    void Lowerer::emit_enumeration_check(
        const RegisterId source,
        const frontend::Type& type) {
        const auto ordinal =
            widen_enumeration_ordinal(source);
        const auto [lower, upper] =
            enumeration_bounds(type);
        process_.operations.emplace_back(
            IntegerCheck{ordinal, lower, upper});
    }



    [[nodiscard]] bool
    Lowerer::validate_static_enumeration_assignment(
        const Expression& expression,
        const frontend::Type& type,
        const frontend::SourceSpan& span) {
        const auto ordinal =
            vhdl_enumeration_ordinal(expression, type);
        if (!ordinal) {
            return true;
        }
        const auto [lower, upper] =
            enumeration_bounds(type);
        if (*ordinal < lower || *ordinal > upper) {
            report(
                "FSIM-ELAB-VHENUMRANGE-004",
                "locally static VHDL enumeration value '"
                    + expression.text
                    + "' is outside subtype range '"
                    + type.enumeration_literals[
                        static_cast<std::size_t>(lower)]
                    + "' to '"
                    + type.enumeration_literals[
                        static_cast<std::size_t>(upper)]
                    + "'",
                span);
            return false;
        }
        return true;
    }



    [[nodiscard]] bool Lowerer::validate_static_integer_assignment(
        const Expression& expression,
        const std::optional<frontend::IntegerRange>& range,
        const frontend::SourceSpan& span) {
        std::string error;
        const auto value =
            evaluate_constant_expression(expression, {}, error);
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
        const std::vector<Statement>& statements) {
        return std::any_of(
            statements.begin(), statements.end(),
            [](const Statement& statement) {
                const auto case_wait =
                    std::any_of(
                        statement.case_alternatives.begin(),
                        statement.case_alternatives.end(),
                        [](const frontend::CaseAlternative& alternative) {
                            return contains_explicit_wait(
                                alternative.statements);
                        });
                return statement.kind == StatementKind::Delay
                    || statement.kind == StatementKind::WaitOn
                    || statement.kind == StatementKind::WaitUntil
                    || contains_explicit_wait(statement.statements)
                    || contains_explicit_wait(statement.else_statements)
                    || case_wait;
            });
    }



    void Lowerer::initialize_variables(
        const std::vector<frontend::VariableDeclaration>& variables) {
        struct Pending {
            const frontend::VariableDeclaration* declaration{};
            RegisterId register_id{};
            std::size_t width{};
        };
        std::vector<Pending> pending;
        pending.reserve(variables.size());
        std::unordered_set<std::string> declared_here;
        for (const auto& variable : variables) {
            if (variable.type.systemverilog_container) {
                if (!declared_here.emplace(variable.name).second) {
                    report(
                        "FSIM-ELAB-SVCONTAINER-001",
                        "duplicate container variable in the same scope '"
                            + variable.name + "'",
                        variable.span);
                    continue;
                }
                const auto type =
                    container_type(variable.type, variable.span);
                if (!type) {
                    continue;
                }
                const auto register_id =
                    allocate_container_register(*type);
                container_locals_.insert_or_assign(
                    variable.name, register_id);
                local_types_.insert_or_assign(
                    variable.name, &variable.type);
                process_.debug_container_locals.push_back(
                    DebugContainerLocal{
                        scoped_local_name(variable.name),
                        register_id,
                        *type,
                        SourceLocation{
                            variable.span.source_name,
                            static_cast<std::uint32_t>(
                                variable.span.begin.line),
                            static_cast<std::uint32_t>(
                                variable.span.begin.column)}});
                if (variable.initializer) {
                    const auto value =
                        type->fixed
                            ? lower_static_container_assignment_value(
                                  *variable.initializer, *type)
                            : lower_container_expression(
                                  *variable.initializer);
                    if (value) {
                        if (process_.container_register_types.at(*value)
                            != *type) {
                            report(
                                variable.initializer->kind
                                        == ExpressionKind::Call
                                    ? "FSIM-ELAB-SVFUNC-008"
                                    : "FSIM-ELAB-SVCONTAINER-010",
                                "container initializer requires an "
                                "exactly compatible kind and profile",
                                variable.initializer->span);
                        } else {
                            process_.operations.emplace_back(
                                CopyContainerRegister{
                                    register_id, *value});
                        }
                    }
                }
                continue;
            }
            if (variable.type.domain
                == frontend::ValueDomain::String) {
                if (!declared_here.emplace(variable.name).second) {
                    report(
                        "FSIM-ELAB-SVSTRING-005",
                        "duplicate string variable in the same scope '"
                            + variable.name + "'",
                        variable.span);
                    continue;
                }
                const auto register_id =
                    allocate_string_register();
                string_locals_.insert_or_assign(
                    variable.name, register_id);
                local_types_.insert_or_assign(
                    variable.name, &variable.type);
                process_.debug_string_locals.push_back(
                    DebugStringLocal{
                        scoped_local_name(variable.name),
                        register_id,
                        SourceLocation{
                            variable.span.source_name,
                            static_cast<std::uint32_t>(
                                variable.span.begin.line),
                            static_cast<std::uint32_t>(
                                variable.span.begin.column)}});
                if (variable.initializer) {
                    const auto value =
                        lower_string_expression(
                            *variable.initializer);
                    if (value) {
                        process_.operations.emplace_back(
                            CopyStringRegister{
                                register_id, *value});
                    }
                } else {
                    process_.operations.emplace_back(
                        LoadStringConstant{register_id, {}});
                }
                continue;
            }
            const auto width = variable.type.width();
            if (!width || *width == 0) {
                report(
                    "FSIM-ELAB-052",
                    "local variable '" + variable.name
                        + "' has no executable packed width",
                    variable.span);
                continue;
            }
            if (!declared_here.emplace(variable.name).second) {
                report(
                    "FSIM-ELAB-053",
                    "duplicate local variable in the same scope '"
                        + variable.name + "'",
                    variable.span);
                continue;
            }
            const auto key = declaration_key(variable);
            const auto register_found = declaration_registers_.find(key);
            RegisterId register_id{};
            if (register_found == declaration_registers_.end()) {
                register_id =
                    allocate_register(*width, variable.type.domain);
                declaration_registers_.emplace(key, register_id);
                auto debug_name = scoped_local_name(variable.name);
                if (!debug_local_names_.emplace(debug_name).second) {
                    debug_name += "@"
                        + std::to_string(variable.span.begin.line)
                        + ":" + std::to_string(
                            variable.span.begin.column);
                    debug_local_names_.emplace(debug_name);
                }
                process_.debug_locals.push_back(DebugLocal{
                    std::move(debug_name),
                    variable.type.spelling,
                    register_id,
                    *width,
                    SourceLocation{
                        variable.span.source_name,
                        static_cast<std::uint32_t>(
                            variable.span.begin.line),
                        static_cast<std::uint32_t>(
                            variable.span.begin.column)},
                    {},
                    {},
                    value_kind(variable.type.domain),
                    {}});
                if (variable.type.integer_range) {
                    const auto [lower, upper] =
                        integer_bounds(variable.type.integer_range);
                    process_.debug_locals.back().integer_lower =
                        lower;
                    process_.debug_locals.back().integer_upper =
                        upper;
                }
                process_.debug_locals.back().enumeration_literals =
                    variable.type.enumeration_literals;
            } else {
                register_id = register_found->second;
            }
            locals_.insert_or_assign(variable.name, register_id);
            local_signed_.insert_or_assign(
                variable.name, variable.type.is_signed);
            local_ranges_.insert_or_assign(
                variable.name, variable.type.packed_range);
            local_integer_ranges_.insert_or_assign(
                variable.name, variable.type.integer_range);
            local_members_.insert_or_assign(
                variable.name, variable.type.packed_members);
            local_types_.insert_or_assign(
                variable.name, &variable.type);
            pending.push_back(Pending{&variable, register_id, *width});
        }
        for (const auto& local : pending) {
            const auto& variable = *local.declaration;
            if (variable.initializer) {
                if (variable.type.domain
                        == frontend::ValueDomain::Integer
                    && !is_integer_expression(
                        *variable.initializer)) {
                    report(
                        "FSIM-ELAB-INTEGER-004",
                        "VHDL integer local initializer requires an "
                        "integer-family expression",
                        variable.span);
                    continue;
                }
                if (variable.type.domain
                        == frontend::ValueDomain::Integer
                    && !validate_static_integer_assignment(
                        *variable.initializer,
                        variable.type.integer_range,
                        variable.span)) {
                    continue;
                }
                if (!variable.type.enumeration_literals.empty()
                    && !validate_static_enumeration_assignment(
                        *variable.initializer,
                        variable.type,
                        variable.span)) {
                    continue;
                }
                auto value =
                    lower_expression(
                        *variable.initializer,
                        local.width,
                        &variable.type);
                if (!value) {
                    continue;
                }
                if (register_width(*value) != local.width
                    && language_
                        != frontend::Language::Vhdl2008) {
                    *value = resize_register(
                        *value,
                        local.width,
                        is_signed_expression(
                            *variable.initializer));
                }
                if (register_width(*value) != local.width) {
                    report(
                        "FSIM-ELAB-054",
                        "local variable initializer width mismatch for '"
                            + variable.name + "'",
                        variable.span);
                    continue;
                }
                if (is_two_state_domain(variable.type.domain)
                    && !is_two_state_domain(
                        register_domain(*value))) {
                    report(
                        "FSIM-ELAB-058",
                        "two-state local variable initializer for '"
                            + variable.name
                            + "' requires an explicit conversion",
                        variable.span);
                    continue;
                }
                if (variable.type.domain
                        == frontend::ValueDomain::Integer) {
                    emit_integer_check(
                        *value, variable.type.integer_range);
                }
                if (!variable.type.enumeration_literals.empty()) {
                    emit_enumeration_check(
                        *value, variable.type);
                }
                process_.operations.emplace_back(
                    CopyRegister{local.register_id, *value});
                continue;
            }
            auto initial_value =
                default_packed_value(variable.type, local.width);
            process_.operations.emplace_back(
                LoadConstant{
                    local.register_id,
                    std::move(initial_value)});
        }
    }



    [[nodiscard]] std::string Lowerer::declaration_key(
        const frontend::VariableDeclaration& variable) {
        return variable.span.source_name + ":"
            + std::to_string(variable.span.begin.offset) + ":"
            + variable.name;
    }



    [[nodiscard]] std::string Lowerer::scoped_local_name(
        const std::string_view name) const {
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
        const Statement& statement) {
        if (!statement.label.empty()) {
            return statement.label;
        }
        return "$block_"
            + std::to_string(statement.span.begin.line) + "_"
            + std::to_string(statement.span.begin.column) + "_"
            + std::to_string(statement.span.begin.offset);
    }



    void Lowerer::lower_block(const Statement& statement) {
        auto outer_locals = locals_;
        auto outer_string_locals = string_locals_;
        auto outer_container_locals = container_locals_;
        auto outer_signed = local_signed_;
        auto outer_ranges = local_ranges_;
        auto outer_integer_ranges = local_integer_ranges_;
        auto outer_members = local_members_;
        auto outer_types = local_types_;
        local_scope_.push_back(block_scope_name(statement));
        initialize_variables(statement.declarations);
        lower_statements(statement.statements);
        local_scope_.pop_back();
        locals_ = std::move(outer_locals);
        string_locals_ = std::move(outer_string_locals);
        container_locals_ = std::move(outer_container_locals);
        local_signed_ = std::move(outer_signed);
        local_ranges_ = std::move(outer_ranges);
        local_integer_ranges_ =
            std::move(outer_integer_ranges);
        local_members_ = std::move(outer_members);
        local_types_ = std::move(outer_types);
    }



    void Lowerer::lower_event_trigger(const Statement& statement) {
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
        const auto current =
            allocate_register(1, frontend::ValueDomain::Logic4);
        const auto toggled =
            allocate_register(1, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(
            ReadSignal{current, found->second});
        process_.operations.emplace_back(
            UnaryNot{toggled, current});
        if (statement.assignment_kind
            == AssignmentKind::NonBlocking) {
            if (statement.delay) {
                process_.operations.emplace_back(
                    WriteAfter{
                        found->second,
                        toggled,
                        statement.delay->magnitude});
            } else {
                process_.operations.emplace_back(
                    WriteUpdate{found->second, toggled});
            }
        } else {
            process_.operations.emplace_back(
                WriteBlocking{found->second, toggled});
        }
    }



    const Statement* Lowerer::recognized_vhdl_edge_guard(
        const frontend::Process& source) {
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
        const auto expected_edge =
            callee == "rising_edge"
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



    void Lowerer::lower_statements(const std::vector<Statement>& statements) {
        for (const auto& statement : statements) {
            lower_statement(statement);
        }
    }



    void Lowerer::lower_statement(const Statement& statement) {
        if (statement.kind != StatementKind::Block
            && !(statement.kind == StatementKind::Loop
                 && statement.loop_runtime)) {
            auto kind = DebugPointKind::statement;
            if (statement.kind == StatementKind::Assert) {
                kind = DebugPointKind::assertion;
            } else if (
                statement.kind == StatementKind::Display
                || statement.kind == StatementKind::FileClose
                || statement.kind == StatementKind::FileDisplay
                || statement.kind == StatementKind::MemoryLoad
                || statement.kind == StatementKind::Report
                || statement.kind == StatementKind::TaskCall
                || statement.kind == StatementKind::ProcedureCall) {
                kind = DebugPointKind::call;
            } else if (
                statement.kind == StatementKind::Delay
                || statement.kind == StatementKind::WaitOn
                || statement.kind == StatementKind::WaitUntil) {
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
            lower_task_call(statement);
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
            process_.operations.emplace_back(WaitFor{statement.delay->magnitude});
            lower_statements(statement.statements);
            break;
        case StatementKind::WaitOn: {
            (void)emit_event_control_wait(statement);
            lower_statements(statement.statements);
            break;
        }
        case StatementKind::WaitUntil:
            lower_wait_until(statement);
            break;
        case StatementKind::EventTrigger:
            lower_event_trigger(statement);
            break;
        case StatementKind::MonitorControl:
            process_.operations.emplace_back(
                MonitorControl{statement.monitor_enabled});
            break;
        case StatementKind::FileClose: {
            const auto* type =
                statement.file_handle.kind
                        == ExpressionKind::Identifier
                    ? object_type(statement.file_handle.text)
                    : nullptr;
            if (type == nullptr
                || type->domain
                    != frontend::ValueDomain::Integer) {
                report(
                    "FSIM-ELAB-SVFILE-001",
                    "$fclose handle must be a 32-bit integer expression",
                    statement.file_handle.span);
                break;
            }
            auto handle =
                lower_expression(statement.file_handle, 32);
            if (!handle) {
                break;
            }
            if (register_width(*handle) != 32) {
                *handle = resize_register(
                    *handle,
                    32,
                    is_signed_expression(statement.file_handle));
            }
            process_.operations.emplace_back(FileClose{*handle});
            break;
        }
        case StatementKind::FileDisplay: {
            const auto* type =
                statement.file_handle.kind
                        == ExpressionKind::Identifier
                    ? object_type(statement.file_handle.text)
                    : nullptr;
            if (type == nullptr
                || type->domain
                    != frontend::ValueDomain::Integer) {
                report(
                    "FSIM-ELAB-SVFILE-001",
                    "file output handle must be a 32-bit integer "
                    "expression",
                    statement.file_handle.span);
                break;
            }
            auto handle =
                lower_expression(statement.file_handle, 32);
            if (!handle) {
                break;
            }
            if (register_width(*handle) != 32) {
                *handle = resize_register(
                    *handle,
                    32,
                    is_signed_expression(statement.file_handle));
            }
            if (!statement.output_format) {
                process_.operations.emplace_back(
                    FileWriteLiteral{
                        *handle,
                        statement.output_text,
                        statement.output_newline});
                break;
            }
            if (*statement.output_format
                    == frontend::OutputFormat::String
                && is_string_expression(statement.value)) {
                const auto source =
                    lower_string_expression(statement.value);
                if (source) {
                    process_.operations.emplace_back(
                        FileWriteString{
                            *handle,
                            *source,
                            statement.output_prefix,
                            statement.output_suffix,
                            statement.output_newline});
                }
                break;
            }
            const auto width =
                infer_width(statement.value).value_or(
                    std::size_t{32});
            const auto source =
                lower_expression(statement.value, width);
            if (!source) {
                report(
                    "FSIM-ELAB-SVFILE-007",
                    "formatted file output value cannot be lowered",
                    statement.value.span);
                break;
            }
            const auto format =
                runtime_output_format(*statement.output_format);
            process_.operations.emplace_back(
                FileWriteFormatted{
                    *handle,
                    *source,
                    static_cast<std::uint32_t>(width),
                    format,
                    statement.output_prefix,
                    statement.output_suffix,
                    statement.output_newline,
                    format == runtime::simir::OutputFormat::decimal
                        && is_signed_expression(statement.value),
                    statement.output_suppress_leading_zero,
                    statement.output_minimum_width,
                    statement.output_left_justify,
                    statement.output_zero_pad});
            break;
        }
        case StatementKind::MemoryLoad: {
            if (language_
                    != frontend::Language::SystemVerilog2017) {
                report(
                    "FSIM-ELAB-SVMEMORY-001",
                    "$readmemb/$readmemh requires "
                    "SystemVerilog-2017",
                    statement.span);
                break;
            }
            const auto path =
                lower_string_expression(statement.value);
            if (!path) {
                report(
                    "FSIM-ELAB-SVMEMORY-002",
                    "read-memory file name must be a string expression",
                    statement.value.span);
                break;
            }
            if (statement.target.kind
                    != ExpressionKind::Identifier) {
                report(
                    "FSIM-ELAB-SVMEMORY-003",
                    "read-memory target must be a direct static-array "
                    "object",
                    statement.target.span);
                break;
            }
            if (read_only_container_objects_.contains(
                    statement.target.text)) {
                report(
                    "FSIM-ELAB-SVPORT-009",
                    "an input container port is read-only within its "
                    "module",
                    statement.target.span);
                break;
            }
            const auto target =
                lower_container_expression(
                    statement.target);
            const auto* source_type =
                object_type(statement.target.text);
            const auto runtime_type =
                source_type
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
                    "$readmemb/$readmemh target must be a bounded static "
                    "unpacked array",
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
            const auto* start_expression =
                statement.task_arguments.empty()
                    ? nullptr
                    : &statement.task_arguments[0];
            const auto* finish_expression =
                statement.task_arguments.size() < 2
                    ? nullptr
                    : &statement.task_arguments[1];
            const auto start =
                lower_bound(start_expression);
            const auto finish =
                lower_bound(finish_expression);
            if ((start_expression && !start)
                || (finish_expression && !finish)) {
                report(
                    "FSIM-ELAB-SVMEMORY-004",
                    "read-memory start and finish must be integral "
                    "expressions",
                    statement.span);
                break;
            }
            process_.operations.emplace_back(
                LoadMemory{
                    *target, *path, start, finish,
                    statement.memory_hex});
            if (const auto object =
                    container_objects_.find(
                        statement.target.text);
                object != container_objects_.end()) {
                process_.operations.emplace_back(
                    WriteContainerObject{
                        object->second, *target});
            }
            break;
        }
        case StatementKind::Display: {
            if (statement.output_monitor) {
                if (statement.output_values.empty()
                    && !statement.output_format) {
                    process_.operations.emplace_back(
                        MonitorInstall{
                            {},
                            statement.output_text,
                            statement.output_newline});
                    break;
                }
                MonitorInstall monitor;
                monitor.newline = statement.output_newline;
                std::string pending_prefix;
                const auto append_value =
                    [&](const frontend::OutputValue& output) {
                        auto prefix =
                            std::move(pending_prefix) + output.prefix;
                        if (output.format
                            == frontend::OutputFormat::Hierarchy) {
                            pending_prefix =
                                std::move(prefix) + hierarchy_;
                            return;
                        }
                        MonitorValue value;
                        value.prefix = std::move(prefix);
                        value.minimum_width = output.minimum_width;
                        value.left_justify = output.left_justify;
                        value.zero_pad = output.zero_pad;
                        value.suppress_leading_zero =
                            output.suppress_leading_zero;
                        if (output.format
                            == frontend::OutputFormat::Time) {
                            value.kind = MonitorValueKind::time;
                        } else {
                            if (output.value.kind
                                != frontend::ExpressionKind::Identifier) {
                                report(
                                    "FSIM-ELAB-103",
                                    "$monitor currently requires direct "
                                    "packed-signal value expressions",
                                    output.value.span);
                                return;
                            }
                            const auto signal =
                                signals_.find(output.value.text);
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
                            value.format =
                                runtime_output_format(output.format);
                            value.signed_decimal =
                                value.format
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
                    monitor.trailing_text =
                        std::move(pending_prefix)
                        + statement.output_trailing_text;
                } else {
                    append_value(
                        frontend::OutputValue{
                            statement.value,
                            *statement.output_format,
                            statement.output_prefix,
                            statement.output_suppress_leading_zero,
                            statement.output_minimum_width,
                            statement.output_left_justify,
                            statement.output_zero_pad});
                    monitor.trailing_text =
                        std::move(pending_prefix)
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
                    const bool last =
                        index + 1 == statement.output_values.size();
                    if (output.format
                        == frontend::OutputFormat::Hierarchy) {
                        process_.operations.emplace_back(
                            Display{
                                output.prefix + hierarchy_
                                    + (last
                                           ? statement.output_trailing_text
                                           : std::string{}),
                                last && statement.output_newline,
                                statement.output_postponed});
                        continue;
                    }
                    if (output.format == frontend::OutputFormat::Time) {
                        process_.operations.emplace_back(
                            TimeDisplay{
                                output.prefix,
                                last
                                    ? statement.output_trailing_text
                                    : std::string{},
                                last && statement.output_newline,
                                statement.output_postponed,
                                output.minimum_width,
                                output.left_justify,
                                output.zero_pad});
                        continue;
                    }
                    if (output.format
                            == frontend::OutputFormat::String
                        && is_string_expression(output.value)) {
                        const auto source =
                            lower_string_expression(output.value);
                        if (!source) {
                            continue;
                        }
                        process_.operations.emplace_back(
                            StringDisplay{
                                *source,
                                output.prefix,
                                last
                                    ? statement.output_trailing_text
                                    : std::string{},
                                last && statement.output_newline,
                                statement.output_postponed});
                        continue;
                    }
                    const auto width =
                        infer_width(output.value)
                            .value_or(std::size_t{32});
                    const auto source =
                        lower_expression(output.value, width);
                    if (!source) {
                        report(
                            "FSIM-ELAB-102",
                            "formatted output value cannot be lowered",
                            output.value.span);
                        continue;
                    }
                    const auto format =
                        runtime_output_format(output.format);
                    process_.operations.emplace_back(
                        FormatDisplay{
                            *source,
                            format,
                            output.prefix,
                            last
                                ? statement.output_trailing_text
                                : std::string{},
                            last && statement.output_newline,
                            statement.output_postponed,
                            format
                                    == runtime::simir::OutputFormat::decimal
                                && is_signed_expression(output.value),
                            output.suppress_leading_zero,
                            output.minimum_width,
                            output.left_justify,
                            output.zero_pad});
                }
                break;
            }
            if (statement.output_format) {
                if (*statement.output_format
                        == frontend::OutputFormat::String
                    && is_string_expression(statement.value)) {
                    const auto source =
                        lower_string_expression(statement.value);
                    if (source) {
                        process_.operations.emplace_back(
                            StringDisplay{
                                *source,
                                statement.output_prefix,
                                statement.output_suffix,
                                statement.output_newline,
                                statement.output_postponed});
                    }
                    break;
                }
                const auto width =
                    infer_width(statement.value)
                        .value_or(std::size_t{32});
                const auto source =
                    lower_expression(statement.value, width);
                if (!source) {
                    report(
                        "FSIM-ELAB-102",
                        "formatted output value cannot be lowered",
                        statement.span);
                    break;
                }
                const auto format =
                    runtime_output_format(*statement.output_format);
                process_.operations.emplace_back(
                    FormatDisplay{
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
                        statement.output_zero_pad});
            } else {
                process_.operations.emplace_back(
                    Display{
                        statement.output_text,
                        statement.output_newline,
                        statement.output_postponed});
            }
            break;
        }
        case StatementKind::Report: {
            AssertionSeverity severity = AssertionSeverity::note;
            switch (statement.assertion_severity) {
            case frontend::AssertionSeverity::Note:
                severity = AssertionSeverity::note;
                break;
            case frontend::AssertionSeverity::Warning:
                severity = AssertionSeverity::warning;
                break;
            case frontend::AssertionSeverity::Error:
                severity = AssertionSeverity::error;
                break;
            case frontend::AssertionSeverity::Failure:
                severity = AssertionSeverity::failure;
                break;
            }
            process_.operations.emplace_back(
                runtime::simir::Report{
                    statement.output_text,
                    severity,
                    SourceLocation{
                        statement.span.source_name,
                        static_cast<std::uint32_t>(
                            statement.span.begin.line),
                        static_cast<std::uint32_t>(
                            statement.span.begin.column)}});
            break;
        }
        case StatementKind::Pause:
            process_.operations.emplace_back(Pause{});
            break;
        case StatementKind::Finish:
            process_.operations.emplace_back(Stop{});
            break;
        case StatementKind::Block:
            lower_block(statement);
            break;
        case StatementKind::Null:
            break;
        }
    }



    void Lowerer::emit_debug_point(
        const DebugPointKind kind,
        const frontend::SourceSpan& span) {
        process_.operations.emplace_back(DebugPoint{
            kind,
            SourceLocation{
                span.source_name,
                static_cast<std::uint32_t>(span.begin.line),
                static_cast<std::uint32_t>(span.begin.column)}});
    }



    void Lowerer::lower_wait_until(const Statement& statement) {
        if (language_ != frontend::Language::Vhdl2008) {
            lower_immediate_condition_wait(statement);
            return;
        }

        const auto condition_operations_start =
            process_.operations.size();
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
            auto resolved =
                resolve_wait_sensitivities(statement);
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
            std::ranges::sort(waited_signals);
            waited_signals.erase(
                std::unique(
                    waited_signals.begin(),
                    waited_signals.end()),
                waited_signals.end());
        }

        if (waited_signals.empty() && !statement.delay) {
            process_.operations.emplace_back(WaitForever{});
            lower_statements(statement.statements);
            return;
        }

        std::optional<RegisterId> timed_out;
        if (statement.delay) {
            timed_out = allocate_register(
                1, frontend::ValueDomain::Boolean);
        }

        const auto initial_wait =
            static_cast<InstructionIndex>(
                process_.operations.size());
        WaitOn first_wait{
            waited_signals, waited_edges};
        if (statement.delay) {
            first_wait.timeout =
                statement.delay->magnitude;
            first_wait.timeout_result = timed_out;
        }
        process_.operations.emplace_back(
            std::move(first_wait));

        std::optional<InstructionIndex> timeout_branch;
        if (timed_out) {
            timeout_branch =
                static_cast<InstructionIndex>(
                    process_.operations.size());
            process_.operations.emplace_back(
                Branch{
                    *timed_out,
                    0,
                    0,
                    UnknownBranchPolicy::error});
        }

        const auto condition_start =
            static_cast<InstructionIndex>(
                process_.operations.size());
        process_.operations.insert(
            process_.operations.end(),
            std::make_move_iterator(
                condition_operations.begin()),
            std::make_move_iterator(
                condition_operations.end()));
        const auto condition_branch =
            static_cast<InstructionIndex>(
                process_.operations.size());
        const auto unknown_policy =
            language_ == frontend::Language::Vhdl2008
                ? UnknownBranchPolicy::error
                : UnknownBranchPolicy::when_false;
        process_.operations.emplace_back(
            Branch{*condition, 0, 0, unknown_policy});

        const auto rewait_start =
            static_cast<InstructionIndex>(
                process_.operations.size());
        WaitOn rewait{
            std::move(waited_signals),
            std::move(waited_edges)};
        if (statement.delay) {
            rewait.timeout =
                statement.delay->magnitude;
            rewait.timeout_result = timed_out;
            rewait.timeout_origin = initial_wait;
        }
        process_.operations.emplace_back(std::move(rewait));
        process_.operations.emplace_back(
            Jump{
                timeout_branch.value_or(
                    condition_start)});

        const auto satisfied_start =
            static_cast<InstructionIndex>(
                process_.operations.size());
        lower_statements(statement.statements);
        process_.operations[condition_branch] = Branch{
            *condition,
            satisfied_start,
            rewait_start,
            unknown_policy};
        if (timeout_branch) {
            process_.operations[*timeout_branch] = Branch{
                *timed_out,
                satisfied_start,
                condition_start,
                UnknownBranchPolicy::error};
        }
    }



    void Lowerer::lower_immediate_condition_wait(
        const Statement& statement) {
        const auto condition_start =
            static_cast<InstructionIndex>(
                process_.operations.size());
        const auto condition = lower_condition(
            statement.condition,
            "FSIM-ELAB-079",
            "wait");
        if (!condition) {
            return;
        }
        const auto branch_index =
            static_cast<InstructionIndex>(
                process_.operations.size());
        process_.operations.emplace_back(
            Branch{
                *condition,
                0,
                0,
                UnknownBranchPolicy::when_false});

        const auto wait_start =
            static_cast<InstructionIndex>(
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
            process_.operations.emplace_back(WaitForever{});
        } else {
            process_.operations.emplace_back(
                WaitOn{std::move(waited_signals)});
            process_.operations.emplace_back(
                Jump{condition_start});
        }

        const auto satisfied_start =
            static_cast<InstructionIndex>(
                process_.operations.size());
        lower_statements(statement.statements);
        process_.operations[branch_index] = Branch{
            *condition,
            satisfied_start,
            wait_start,
            UnknownBranchPolicy::when_false};
    }

} // namespace fsim::elaboration
