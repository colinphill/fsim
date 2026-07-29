// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

Lowerer::Lowerer(
        ElaboratedDesign& design,
        const std::unordered_map<std::string, SignalId>& signals,
        const std::unordered_map<
            std::string, const frontend::Type*>& visible_types,
        const std::unordered_map<
            std::string, const frontend::Type*>& visible_type_marks,
        std::vector<Diagnostic>& diagnostics)
        : design_(design),
          signals_(signals),
          visible_types_(visible_types),
          visible_type_marks_(visible_type_marks),
          diagnostics_(diagnostics) {}



    Process Lowerer::lower_process(
        const frontend::Process& source,
        const frontend::Language language,
        const std::string_view hierarchy) {
        process_ = Process{};
        language_ = language;
        hierarchy_ = std::string{hierarchy};
        next_register_ = 0;
        register_widths_.clear();
        register_domains_.clear();
        locals_.clear();
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
        initialize_variables(source.variables);
        bool wildcard_sensitivity = false;
        for (const auto& sensitivity : source.sensitivities) {
            if (sensitivity.signal == "*") {
                wildcard_sensitivity = true;
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
            collect_statement_identifiers(
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
        if (waits_before_first_execution) {
            process_.operations.emplace_back(WaitSensitivity{});
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
        process_.register_count = next_register_;
        process_.register_value_kinds.reserve(register_domains_.size());
        for (const auto domain : register_domains_) {
            process_.register_value_kinds.push_back(
                value_kind(domain));
        }
        next_register_ = 0;
        register_widths_.clear();
        register_domains_.clear();
        locals_.clear();
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
        next_register_ = 0;
        register_widths_.clear();
        register_domains_.clear();
        locals_.clear();
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
        emit_debug_point(DebugPointKind::process_entry, statement.span);

        std::set<std::string> dependencies;
        collect_statement_identifiers(
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
        process_.register_count = next_register_;
        process_.register_value_kinds.reserve(register_domains_.size());
        for (const auto domain : register_domains_) {
            process_.register_value_kinds.push_back(
                value_kind(domain));
        }
        next_register_ = 0;
        register_widths_.clear();
        register_domains_.clear();
        locals_.clear();
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
                const auto value =
                    lower_expression(
                        *variable.initializer,
                        local.width,
                        &variable.type);
                if (!value) {
                    continue;
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
                || statement.kind == StatementKind::Report) {
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
            auto [signals, edges] =
                resolve_wait_sensitivities(statement);
            if (!signals.empty() || statement.delay) {
                WaitOn wait{
                    std::move(signals), std::move(edges)};
                if (statement.delay) {
                    wait.timeout = statement.delay->magnitude;
                }
                process_.operations.emplace_back(std::move(wait));
            }
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
        case StatementKind::Display: {
            const auto runtime_output_format =
                [](const frontend::OutputFormat source) {
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
                    throw std::logic_error{
                        "invalid frontend output format"};
                };
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



    [[nodiscard]] std::pair<
        std::vector<SignalId>,
        std::vector<runtime::simir::EdgeKind>>
    Lowerer::resolve_wait_sensitivities(
        const Statement& statement) {
        std::vector<SignalId> signals;
        std::vector<runtime::simir::EdgeKind> edges;
        signals.reserve(statement.sensitivities.size());
        edges.reserve(statement.sensitivities.size());
        for (const auto& sensitivity : statement.sensitivities) {
            if (sensitivity.signal == "*") {
                std::set<std::string> dependencies;
                collect_statement_identifiers(
                    statement.statements, dependencies);
                if (statement.kind == StatementKind::Assignment) {
                    collect_identifiers(
                        statement.value, dependencies);
                }
                for (const auto& dependency : dependencies) {
                    if (locals_.contains(dependency)) {
                        continue;
                    }
                    if (const auto found =
                            signals_.find(dependency);
                        found != signals_.end()) {
                        signals.push_back(found->second);
                        edges.push_back(
                            runtime::simir::EdgeKind::any);
                    }
                }
                if (dependencies.empty() || signals.empty()) {
                    report(
                        "FSIM-ELAB-062",
                        "dynamic wildcard event control has no readable "
                        "signal dependencies",
                        sensitivity.span);
                }
                continue;
            }
            const auto found = signals_.find(sensitivity.signal);
            if (found == signals_.end()) {
                report(
                    "FSIM-ELAB-059",
                    "unknown wait signal '" + sensitivity.signal + "'",
                    sensitivity.span);
                continue;
            }
            if (sensitivity.edge != frontend::EdgeKind::Any
                && design_.signal_info_[found->second].width != 1) {
                report(
                    "FSIM-ELAB-060",
                    "dynamic edge-qualified wait signal '"
                        + sensitivity.signal
                        + "' must be scalar",
                    sensitivity.span);
                continue;
            }
            signals.push_back(found->second);
            auto edge = runtime::simir::EdgeKind::any;
            if (sensitivity.edge
                == frontend::EdgeKind::Positive) {
                edge = runtime::simir::EdgeKind::posedge;
            } else if (
                sensitivity.edge
                == frontend::EdgeKind::Negative) {
                edge = runtime::simir::EdgeKind::negedge;
            }
            edges.push_back(edge);
        }
        return {std::move(signals), std::move(edges)};
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
