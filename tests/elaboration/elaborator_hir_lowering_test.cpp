// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include "fsim/semantic/compiled_design_linker.hpp"
#include "fsim/semantic/compiled_design_specialization.hpp"

namespace fsim::tests::elaboration {
namespace {

semantic::SourceSpanId add_source_span(
    semantic::Model& model,
    const semantic::SourceFileId file,
    const frontend::SourceSpan& source)
{
    return model.intern_source_span(
        file,
        source.source_name.str(),
        {
            static_cast<std::uint64_t>(source.begin.offset),
            static_cast<std::uint32_t>(source.begin.line),
            static_cast<std::uint32_t>(source.begin.column),
        },
        {
            static_cast<std::uint64_t>(source.end.offset),
            static_cast<std::uint32_t>(source.end.line),
            static_cast<std::uint32_t>(source.end.column),
        });
}

fsim::elaboration::ElaborationResult elaborate_compiled(
    semantic::CompiledDesign& compiled,
    const std::string_view target,
    const std::string_view alias)
{
    compiled.refresh_lookup_indexes();
    const fsim::elaboration::Root root {
        std::string { target }, std::string { alias } };
    return fsim::elaboration::elaborate(
        compiled,
        std::span<const fsim::elaboration::Root> { &root, 1U },
        { },
        { },
        nullptr,
        { });
}

struct SystemVerilogHirFixture {
    semantic::CompiledDesign compiled;
    semantic::ExpressionId literal;
    semantic::ProcessId process;
    semantic::SourceSpanId process_source;
};

SystemVerilogHirFixture systemverilog_hir_fixture(
    const bool supported,
    const bool residual = false)
{
    const auto parsed = frontend::parse_text(
        "hir_direct.sv",
        "module hir_direct #(parameter bit P = 1'b1)"
        "(output logic q); initial q = 1'b1; endmodule",
        frontend::Language::SystemVerilog2017);
    assert(parsed.ok() && parsed.design.units.size() == 1U);
    const auto& unit_adapter = parsed.design.units.front();
    assert(unit_adapter.processes.size() == 1U);
    const auto& process_adapter = unit_adapter.processes.front();
    assert(process_adapter.statements.size() == 1U);

    semantic::Model model;
    semantic::sv::Hir systemverilog;
    semantic::vhdl::Hir vhdl;
    const auto file = model.intern_source_file(
        "hir_direct.sv", "hir-direct-digest");
    const auto unit_source = add_source_span(
        model, file, unit_adapter.span);
    const auto process_source = add_source_span(
        model, file, process_adapter.span);
    const auto statement_source = add_source_span(
        model, file, process_adapter.statements.front().span);
    const auto origin = model.add_origin(
        semantic::OriginKind::parsed, unit_source);
    const auto unit = model.add_unit(
        semantic::Language::system_verilog,
        semantic::UnitKind::verilog_module,
        "work",
        "hir_direct",
        { },
        unit_source,
        origin);
    const auto scope = model.units()[unit.value()].scope;
    const auto parameter = model.add_declaration(
        scope,
        semantic::DeclarationKind::constant,
        "P",
        unit_source,
        origin);
    semantic::sv::Declaration parameter_declaration;
    parameter_declaration.id = parameter;
    parameter_declaration.scope = scope;
    parameter_declaration.form = semantic::sv::DeclarationForm::parameter;
    parameter_declaration.name = "P";
    parameter_declaration.source = unit_source;
    parameter_declaration.origin = origin;
    parameter_declaration.type.emplace();
    parameter_declaration.type->target.source = unit_source;
    parameter_declaration.type->target.spelling = "bit";
    parameter_declaration.type->executable_width = 1U;
    systemverilog.mutable_declarations().push_back(
        std::move(parameter_declaration));
    const auto q = model.add_declaration(
        scope,
        semantic::DeclarationKind::signal,
        "q",
        unit_source,
        origin);

    semantic::sv::Declaration q_declaration;
    q_declaration.id = q;
    q_declaration.scope = scope;
    q_declaration.form = semantic::sv::DeclarationForm::port;
    q_declaration.name = "q";
    q_declaration.source = unit_source;
    q_declaration.origin = origin;
    q_declaration.direction = semantic::sv::Direction::output;
    q_declaration.type.emplace();
    q_declaration.type->target.source = unit_source;
    q_declaration.type->target.spelling = "logic";
    q_declaration.type->executable_width = 1U;
    q_declaration.type->four_state = true;
    systemverilog.mutable_declarations().push_back(q_declaration);

    const auto target = model.add_expression_identity(
        scope, statement_source, origin);
    semantic::sv::Expression target_expression;
    target_expression.id = target;
    target_expression.scope = scope;
    target_expression.kind = semantic::sv::ExpressionKind::name;
    target_expression.text = "q";
    target_expression.source = statement_source;
    target_expression.origin = origin;
    target_expression.referenced_name.emplace();
    target_expression.referenced_name->spelling = "q";
    target_expression.referenced_name->source = statement_source;
    target_expression.referenced_name->selected = q;
    systemverilog.mutable_expressions().push_back(target_expression);

    const auto literal = model.add_expression_identity(
        scope, statement_source, origin);
    semantic::sv::Expression literal_expression;
    literal_expression.id = literal;
    literal_expression.scope = scope;
    literal_expression.kind = semantic::sv::ExpressionKind::integer_literal;
    literal_expression.text = "1'b1";
    literal_expression.source = statement_source;
    literal_expression.origin = origin;
    if (residual) {
        literal_expression.dependencies.parameters.push_back(parameter);
    }
    systemverilog.mutable_expressions().push_back(literal_expression);

    const auto statement = model.add_statement_identity(
        scope, statement_source, origin);
    semantic::sv::Statement statement_record;
    statement_record.id = statement;
    statement_record.scope = scope;
    statement_record.kind = supported
        ? semantic::sv::StatementKind::assignment
        : semantic::sv::StatementKind::wait_order;
    statement_record.source = statement_source;
    statement_record.origin = origin;
    if (supported) {
        statement_record.target = target;
        statement_record.value = literal;
    }
    systemverilog.mutable_statements().push_back(statement_record);

    const auto process = model.add_process_identity(
        scope, process_adapter.name, process_source, origin);
    semantic::sv::Process process_record;
    process_record.id = process;
    process_record.scope = scope;
    process_record.kind = semantic::sv::ProcessKind::initial;
    process_record.name = process_adapter.name;
    process_record.source = process_source;
    process_record.origin = origin;
    process_record.statements.push_back(statement);
    systemverilog.mutable_processes().push_back(process_record);

    semantic::sv::Unit unit_record;
    unit_record.id = unit;
    unit_record.scope = scope;
    unit_record.kind = semantic::sv::UnitKind::module;
    unit_record.library = "work";
    unit_record.name = "hir_direct";
    unit_record.source = unit_source;
    unit_record.origin = origin;
    unit_record.declarations = { parameter, q };
    unit_record.processes.push_back(process);
    systemverilog.mutable_units().push_back(unit_record);

    semantic::CompiledDesign compiled {
        std::move(model), std::move(systemverilog), std::move(vhdl) };
    assert(compiled.valid());
    return {
        std::move(compiled),
        literal,
        process,
        process_source,
    };
}

semantic::ExpressionId add_systemverilog_expression(
    SystemVerilogHirFixture& fixture,
    const semantic::sv::ExpressionKind kind,
    const std::string_view text,
    std::vector<semantic::ExpressionId> operands = { })
{
    const auto source = fixture.compiled.find_expression(fixture.literal);
    assert(source && source->systemverilog != nullptr);
    auto expression = *source->systemverilog;
    expression.id = fixture.compiled.semantics.add_expression_identity(
        expression.scope, expression.source, expression.origin);
    expression.kind = kind;
    expression.text = text;
    expression.operands = std::move(operands);
    expression.dependencies = { };
    fixture.compiled.systemverilog_hir.mutable_expressions().push_back(
        expression);
    return expression.id;
}

semantic::StatementId clone_systemverilog_assignment(
    SystemVerilogHirFixture& fixture)
{
    const auto source = fixture.compiled.find_process(fixture.process);
    assert(source && source->systemverilog != nullptr);
    const auto original = source->systemverilog->statements.front();
    const auto found = fixture.compiled.find_statement(original);
    assert(found && found->systemverilog != nullptr);
    auto statement = *found->systemverilog;
    statement.id = fixture.compiled.semantics.add_statement_identity(
        statement.scope, statement.source, statement.origin);
    fixture.compiled.systemverilog_hir.mutable_statements().push_back(
        statement);
    return statement.id;
}

semantic::StatementId add_systemverilog_loop(
    SystemVerilogHirFixture& fixture,
    const semantic::ExpressionId initial,
    const semantic::ExpressionId limit,
    const semantic::StatementId body,
    const bool repeat,
    const bool runtime = false,
    const bool post_test = false)
{
    const auto source = fixture.compiled.find_process(fixture.process);
    assert(source && source->systemverilog != nullptr);
    const auto original = fixture.compiled.find_statement(
        source->systemverilog->statements.front());
    assert(original && original->systemverilog != nullptr);
    semantic::sv::Statement loop;
    loop.id = fixture.compiled.semantics.add_statement_identity(
        original->systemverilog->scope,
        original->systemverilog->source,
        original->systemverilog->origin);
    loop.scope = original->systemverilog->scope;
    loop.kind = semantic::sv::StatementKind::loop;
    loop.source = original->systemverilog->source;
    loop.origin = original->systemverilog->origin;
    loop.statements.push_back(body);
    if (runtime) {
        loop.condition = limit;
        loop.loop_runtime = true;
        loop.loop_post_test = post_test;
    } else {
        loop.loop_initial = initial;
        loop.loop_limit = limit;
        loop.loop_repeat = repeat;
        loop.loop_limit_exclusive = repeat;
    }
    fixture.compiled.systemverilog_hir.mutable_statements().push_back(loop);
    return loop.id;
}

void run_systemverilog_direct_hir_test()
{
    auto fixture = systemverilog_hir_fixture(true);
    auto result = elaborate_compiled(
        fixture.compiled, "sv:work.hir_direct", "hir_direct");
    assert(result.ok() && result.design);
    assert(result.design->processes().size() == 1U);
    const auto& process = result.design->processes().front();
    assert(process.static_sensitivity.empty());
    assert(std::ranges::any_of(
        process.operations,
        [](const runtime::simir::Operation& operation) {
            return runtime::simir::operation_get_if<
                       runtime::simir::WriteBlocking>(&operation)
                != nullptr;
        }));
    assert(process.driver_regions.size() == 1U);
    const auto debug = std::ranges::find_if(
        process.operations,
        [](const runtime::simir::Operation& operation) {
            return runtime::simir::operation_get_if<
                       runtime::simir::DebugPoint>(&operation)
                != nullptr;
        });
    assert(debug != result.design->processes().front().operations.end());
    const auto* point = runtime::simir::operation_get_if<
        runtime::simir::DebugPoint>(&*debug);
    assert(point && point->source.path == "hir_direct.sv");
    const auto q = result.design->find_signal("q");
    assert(q);
    auto interpreter = result.design->create_interpreter();
    interpreter->start();
    (void)interpreter->run();
    assert(interpreter->signal_value(*q).to_msb_string() == "1");

    auto working = semantic::make_specialized_hir_unit(
        fixture.compiled,
        fixture.compiled.systemverilog_units().front().id,
        std::span<const semantic::SpecializedHirActualIdentity> { });
    assert(working);
    auto replacement = *working->find_expression(
        fixture.literal)->systemverilog;
    replacement.text = "1'b0";
    assert(working->replace(replacement));
    assert(working->find_expression(fixture.literal)->systemverilog->text
        == "1'b0");
}

void run_systemverilog_unsized_arithmetic_test()
{
    auto fixture = systemverilog_hir_fixture(true);
    const auto left = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::integer_literal,
        "40");
    const auto right = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::integer_literal,
        "2");
    const auto sum = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::binary,
        "+",
        { left, right });
    const auto process = fixture.compiled.find_process(fixture.process);
    assert(process && process->systemverilog != nullptr);
    const auto statement_id = process->systemverilog->statements.front();
    const auto statement = std::ranges::find(
        fixture.compiled.systemverilog_hir.mutable_statements(),
        statement_id,
        &semantic::sv::Statement::id);
    assert(statement
        != fixture.compiled.systemverilog_hir.mutable_statements().end());
    statement->value = sum;

    const auto result = elaborate_compiled(
        fixture.compiled,
        "sv:work.hir_direct",
        "hir_direct");
    assert(result.ok() && result.design);
    assert(result.design->processes().size() == 1U);
    assert(std::ranges::any_of(
        result.design->processes().front().operations,
        [](const runtime::simir::Operation& operation) {
            const auto* binary = runtime::simir::operation_get_if<
                runtime::simir::Binary>(&operation);
            return binary != nullptr
                && binary->operation
                    == runtime::simir::BinaryOperator::add_unsigned;
        }));
}

void run_systemverilog_procedural_update_test()
{
    auto fixture = systemverilog_hir_fixture(true);
    auto& declarations
        = fixture.compiled.systemverilog_hir.mutable_declarations();
    const auto q_declaration = std::ranges::find(
        declarations, "q", &semantic::sv::Declaration::name);
    assert(q_declaration != declarations.end()
        && q_declaration->type);
    q_declaration->type->executable_width = 4U;

    const auto process_view = fixture.compiled.find_process(
        fixture.process);
    assert(process_view && process_view->systemverilog != nullptr);
    const auto initial_statement_id
        = process_view->systemverilog->statements.front();
    auto& statements
        = fixture.compiled.systemverilog_hir.mutable_statements();
    const auto initial_statement = std::ranges::find(
        statements, initial_statement_id, &semantic::sv::Statement::id);
    assert(initial_statement != statements.end()
        && initial_statement->target && initial_statement->value);
    const auto q_target = *initial_statement->target;
    const auto source = initial_statement->source;
    const auto origin = initial_statement->origin;
    const auto scope = initial_statement->scope;
    const auto one = std::ranges::find(
        fixture.compiled.systemverilog_hir.mutable_expressions(),
        *initial_statement->value,
        &semantic::sv::Expression::id);
    assert(one
        != fixture.compiled.systemverilog_hir.mutable_expressions().end());
    one->text = "4'd1";

    const auto two = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::integer_literal,
        "4'd2");
    const auto sum = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::binary,
        "+",
        { q_target, two });
    const auto compound_id = clone_systemverilog_assignment(fixture);
    const auto compound = std::ranges::find(
        statements, compound_id, &semantic::sv::Statement::id);
    assert(compound != statements.end());
    compound->value = sum;
    compound->update_kind = semantic::sv::UpdateKind::compound;
    compound->update_operator = "+";

    const auto unit_scope
        = fixture.compiled.systemverilog_units().front().scope;
    const auto result_declaration
        = fixture.compiled.semantics.add_declaration(
            unit_scope,
            semantic::DeclarationKind::signal,
            "result",
            source,
            origin);
    auto result_record = *q_declaration;
    result_record.id = result_declaration;
    result_record.name = "result";
    declarations.push_back(result_record);
    fixture.compiled.systemverilog_hir.mutable_units()
        .front()
        .declarations.push_back(result_declaration);

    const auto q_expression = fixture.compiled.find_expression(q_target);
    assert(q_expression && q_expression->systemverilog != nullptr);
    auto result_target_record = *q_expression->systemverilog;
    result_target_record.id
        = fixture.compiled.semantics.add_expression_identity(
            scope, source, origin);
    result_target_record.text = "result";
    assert(result_target_record.referenced_name);
    result_target_record.referenced_name->spelling = "result";
    result_target_record.referenced_name->selected = result_declaration;
    const auto result_target = result_target_record.id;
    fixture.compiled.systemverilog_hir.mutable_expressions().push_back(
        result_target_record);
    const auto postfix = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::update,
        "post++",
        { q_target });
    const auto postfix_assignment_id
        = clone_systemverilog_assignment(fixture);
    const auto postfix_assignment = std::ranges::find(
        statements,
        postfix_assignment_id,
        &semantic::sv::Statement::id);
    assert(postfix_assignment != statements.end());
    postfix_assignment->target = result_target;
    postfix_assignment->value = postfix;

    auto& processes = fixture.compiled.systemverilog_hir.mutable_processes();
    const auto process = std::ranges::find(
        processes, fixture.process, &semantic::sv::Process::id);
    assert(process != processes.end());
    process->statements = {
        initial_statement_id,
        compound_id,
        postfix_assignment_id,
    };
    assert(fixture.compiled.valid());

    auto elaborated = elaborate_compiled(
        fixture.compiled, "sv:work.hir_direct", "hir_direct");
    assert(elaborated.ok() && elaborated.design);
    const auto q = elaborated.design->find_signal("q");
    const auto result = elaborated.design->find_signal("result");
    assert(q && result);
    auto interpreter = elaborated.design->create_interpreter();
    interpreter->start();
    (void)interpreter->run();
    assert(interpreter->signal_value(*q).to_msb_string() == "0100");
    assert(interpreter->signal_value(*result).to_msb_string() == "0011");
}

void run_systemverilog_operator_and_nested_update_test()
{
    {
        auto fixture = systemverilog_hir_fixture(true);
        auto& q = fixture.compiled.systemverilog_hir
                      .mutable_declarations()
                      .back();
        assert(q.type);
        q.type->executable_width = 4U;
        auto& statements
            = fixture.compiled.systemverilog_hir.mutable_statements();
        const auto left = add_systemverilog_expression(
            fixture,
            semantic::sv::ExpressionKind::logic_literal,
            "4'b1010");
        const auto right = add_systemverilog_expression(
            fixture,
            semantic::sv::ExpressionKind::logic_literal,
            "4'b1100");
        const auto xnor_value = add_systemverilog_expression(
            fixture,
            semantic::sv::ExpressionKind::binary,
            "~^",
            { left, right });
        const auto reduced = add_systemverilog_expression(
            fixture,
            semantic::sv::ExpressionKind::unary,
            "|",
            { xnor_value });
        statements.front().value = xnor_value;
        const auto reduction_statement
            = clone_systemverilog_assignment(fixture);
        const auto reduction = std::ranges::find(
            statements,
            reduction_statement,
            &semantic::sv::Statement::id);
        assert(reduction != statements.end());
        reduction->value = reduced;
        fixture.compiled.systemverilog_hir.mutable_processes()
            .front()
            .statements.push_back(reduction_statement);

        const auto elaborated = elaborate_compiled(
            fixture.compiled, "sv:work.hir_direct", "hir_direct");
        assert(elaborated.ok() && elaborated.design);
        const auto& operations
            = elaborated.design->processes().front().operations;
        assert(std::ranges::any_of(
            operations,
            [](const runtime::simir::Operation& operation) {
                const auto* binary = runtime::simir::operation_get_if<
                    runtime::simir::Binary>(&operation);
                return binary != nullptr
                    && binary->operation
                        == runtime::simir::BinaryOperator::bit_xor;
            }));
        assert(std::ranges::any_of(
            operations,
            [](const runtime::simir::Operation& operation) {
                const auto* reduction_operation = runtime::simir::operation_get_if<
                    runtime::simir::Reduction>(&operation);
                return reduction_operation != nullptr
                    && reduction_operation->operation
                        == runtime::simir::ReductionOperator::bit_or;
            }));
        const auto q_signal = elaborated.design->find_signal("q");
        assert(q_signal);
        auto interpreter = elaborated.design->create_interpreter();
        interpreter->start();
        (void)interpreter->run();
        assert(interpreter->signal_value(*q_signal).to_msb_string()
            == "0001");
    }

    {
        auto fixture = systemverilog_hir_fixture(true);
        auto& q = fixture.compiled.systemverilog_hir
                      .mutable_declarations()
                      .back();
        assert(q.type);
        q.type->executable_width = 8U;
        auto& expressions
            = fixture.compiled.systemverilog_hir.mutable_expressions();
        const auto process = fixture.compiled.find_process(fixture.process);
        assert(process && process->systemverilog != nullptr);
        auto& statements
            = fixture.compiled.systemverilog_hir.mutable_statements();
        const auto initial = std::ranges::find(
            statements,
            process->systemverilog->statements.front(),
            &semantic::sv::Statement::id);
        assert(initial != statements.end() && initial->target
            && initial->value);
        const auto q_target = *initial->target;
        const auto literal = std::ranges::find(
            expressions,
            *initial->value,
            &semantic::sv::Expression::id);
        assert(literal != expressions.end());
        literal->text = "8'b10100000";
        const auto seven = add_systemverilog_expression(
            fixture,
            semantic::sv::ExpressionKind::integer_literal,
            "7");
        const auto two = add_systemverilog_expression(
            fixture,
            semantic::sv::ExpressionKind::integer_literal,
            "2");
        const auto three = add_systemverilog_expression(
            fixture,
            semantic::sv::ExpressionKind::integer_literal,
            "3");
        const auto one = add_systemverilog_expression(
            fixture,
            semantic::sv::ExpressionKind::integer_literal,
            "1");
        const auto inner = add_systemverilog_expression(
            fixture,
            semantic::sv::ExpressionKind::slice,
            ":",
            { q_target, seven, two });
        const auto outer = add_systemverilog_expression(
            fixture,
            semantic::sv::ExpressionKind::slice,
            ":",
            { inner, three, one });
        const auto increment = add_systemverilog_expression(
            fixture,
            semantic::sv::ExpressionKind::logic_literal,
            "3'b001");
        const auto sum = add_systemverilog_expression(
            fixture,
            semantic::sv::ExpressionKind::binary,
            "+",
            { outer, increment });
        const auto update_statement
            = clone_systemverilog_assignment(fixture);
        const auto update = std::ranges::find(
            statements,
            update_statement,
            &semantic::sv::Statement::id);
        assert(update != statements.end());
        update->target = outer;
        update->value = sum;
        update->update_kind = semantic::sv::UpdateKind::compound;
        update->update_operator = "+";
        fixture.compiled.systemverilog_hir.mutable_processes()
            .front()
            .statements.push_back(update_statement);

        const auto elaborated = elaborate_compiled(
            fixture.compiled, "sv:work.hir_direct", "hir_direct");
        assert(elaborated.ok() && elaborated.design);
        const auto q_signal = elaborated.design->find_signal("q");
        assert(q_signal);
        auto interpreter = elaborated.design->create_interpreter();
        interpreter->start();
        (void)interpreter->run();
        assert(interpreter->signal_value(*q_signal).to_msb_string()
            == "10101000");
    }
}

void run_systemverilog_formatted_string_hir_test()
{
    auto fixture = systemverilog_hir_fixture(true);
    auto& process
        = fixture.compiled.systemverilog_hir.mutable_processes().front();
    const auto formatted = fixture.compiled.semantics.add_declaration(
        process.scope,
        semantic::DeclarationKind::variable,
        "formatted",
        process.source,
        process.origin);
    semantic::sv::Declaration formatted_record;
    formatted_record.id = formatted;
    formatted_record.scope = process.scope;
    formatted_record.form = semantic::sv::DeclarationForm::variable;
    formatted_record.name = "formatted";
    formatted_record.source = process.source;
    formatted_record.origin = process.origin;
    formatted_record.type.emplace();
    formatted_record.type->target.source = process.source;
    formatted_record.type->target.spelling = "string";
    formatted_record.type->value_form = semantic::sv::TypeForm::string;
    fixture.compiled.systemverilog_hir.mutable_declarations().push_back(
        formatted_record);
    process.declarations.push_back(formatted);

    const auto formatted_target = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::name,
        "formatted");
    auto& expressions
        = fixture.compiled.systemverilog_hir.mutable_expressions();
    const auto target = std::ranges::find(
        expressions,
        formatted_target,
        &semantic::sv::Expression::id);
    assert(target != expressions.end());
    target->referenced_name.emplace();
    target->referenced_name->spelling = "formatted";
    target->referenced_name->source = process.source;
    target->referenced_name->selected = formatted;
    const auto format = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::string_literal,
        "\"%m/%0t/%0h\"");
    const auto format_record = std::ranges::find(
        expressions, format, &semantic::sv::Expression::id);
    assert(format_record != expressions.end());
    format_record->decoded_string = "%m/%0t/%0h";
    const auto call = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::call,
        "$sformatf",
        { format, fixture.literal });
    const auto assignment = clone_systemverilog_assignment(fixture);
    auto& statements
        = fixture.compiled.systemverilog_hir.mutable_statements();
    const auto assignment_record = std::ranges::find(
        statements, assignment, &semantic::sv::Statement::id);
    assert(assignment_record != statements.end());
    assignment_record->target = formatted_target;
    assignment_record->value = call;
    process.statements.insert(process.statements.begin(), assignment);

    const auto elaborated = elaborate_compiled(
        fixture.compiled, "sv:work.hir_direct", "hir_direct");
    assert(elaborated.ok() && elaborated.design);
    const auto& operations
        = elaborated.design->processes().front().operations;
    assert(std::ranges::any_of(
        operations,
        [](const runtime::simir::Operation& operation) {
            const auto* method = runtime::simir::operation_get_if<
                runtime::simir::StringMethod>(&operation);
            return method != nullptr
                && method->operation
                    == runtime::simir::StringMethodOperator::format_time
                && method->suppress_leading_zero
                && !method->use_timeformat_width;
        }));
    assert(std::ranges::any_of(
        operations,
        [](const runtime::simir::Operation& operation) {
            const auto* method = runtime::simir::operation_get_if<
                runtime::simir::StringMethod>(&operation);
            return method != nullptr
                && method->operation
                    == runtime::simir::StringMethodOperator::format_packed;
        }));
}

void run_systemverilog_system_function_assignment_test()
{
    auto fixture = systemverilog_hir_fixture(true);
    const auto string = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::string_literal,
        "\"command\"");
    const auto string_record = std::ranges::find(
        fixture.compiled.systemverilog_hir.mutable_expressions(),
        string,
        &semantic::sv::Expression::id);
    assert(string_record
        != fixture.compiled.systemverilog_hir.mutable_expressions().end());
    string_record->decoded_string = "command";
    const auto system = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::call,
        "$system",
        { string });
    const auto random = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::call,
        "$urandom");
    const auto unknown = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::call,
        "$isunknown",
        { fixture.literal });

    auto& process
        = fixture.compiled.systemverilog_hir.mutable_processes().front();
    const auto scope = process.scope;
    const auto source = process.source;
    const auto origin = process.origin;
    const auto randomized = fixture.compiled.semantics.add_declaration(
        scope,
        semantic::DeclarationKind::variable,
        "randomized",
        source,
        origin);
    semantic::sv::Declaration randomized_record;
    randomized_record.id = randomized;
    randomized_record.scope = scope;
    randomized_record.form = semantic::sv::DeclarationForm::variable;
    randomized_record.name = "randomized";
    randomized_record.source = source;
    randomized_record.origin = origin;
    randomized_record.type.emplace();
    randomized_record.type->target.source = source;
    randomized_record.type->target.spelling = "logic";
    randomized_record.type->executable_width = 3U;
    randomized_record.type->four_state = true;
    randomized_record.type->packed_range.emplace();
    randomized_record.type->packed_range->left = 2;
    randomized_record.type->packed_range->right = 0;
    randomized_record.type->packed_range->descending = true;
    randomized_record.type->packed_range->source = source;
    fixture.compiled.systemverilog_hir.mutable_declarations().push_back(
        std::move(randomized_record));
    process.declarations.push_back(randomized);
    const auto randomized_name = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::name,
        "randomized");
    const auto randomized_name_record = std::ranges::find(
        fixture.compiled.systemverilog_hir.mutable_expressions(),
        randomized_name,
        &semantic::sv::Expression::id);
    assert(randomized_name_record
        != fixture.compiled.systemverilog_hir.mutable_expressions().end());
    randomized_name_record->referenced_name.emplace();
    randomized_name_record->referenced_name->spelling = "randomized";
    randomized_name_record->referenced_name->source = source;
    randomized_name_record->referenced_name->selected = randomized;
    const auto scope_randomize = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::call,
        "std::randomize",
        { randomized_name });

    auto& statements
        = fixture.compiled.systemverilog_hir.mutable_statements();
    statements.front().value = unknown;
    const auto random_statement = clone_systemverilog_assignment(fixture);
    const auto scope_randomize_statement
        = clone_systemverilog_assignment(fixture);
    const auto system_statement = clone_systemverilog_assignment(fixture);
    const auto system_task = clone_systemverilog_assignment(fixture);
    const auto random_record = std::ranges::find(
        statements, random_statement, &semantic::sv::Statement::id);
    const auto scope_randomize_record = std::ranges::find(
        statements,
        scope_randomize_statement,
        &semantic::sv::Statement::id);
    const auto system_record = std::ranges::find(
        statements, system_statement, &semantic::sv::Statement::id);
    const auto task_record = std::ranges::find(
        statements, system_task, &semantic::sv::Statement::id);
    assert(random_record != statements.end());
    assert(scope_randomize_record != statements.end());
    assert(system_record != statements.end());
    assert(task_record != statements.end());
    random_record->value = random;
    scope_randomize_record->value = scope_randomize;
    system_record->value = system;
    task_record->kind = semantic::sv::StatementKind::task_call;
    task_record->target.reset();
    task_record->value.reset();
    task_record->task.spelling = "$system";
    task_record->task.source = task_record->source;
    task_record->task_arguments.push_back({
        std::nullopt, string, task_record->source });
    fixture.compiled.systemverilog_hir.mutable_processes().front().statements
        = {
              statements.front().id,
              random_statement,
              scope_randomize_statement,
              system_statement,
              system_task,
          };

    const auto result = elaborate_compiled(
        fixture.compiled, "sv:work.hir_direct", "hir_direct");
    assert(result.ok() && result.design);
    const auto& operations = result.design->processes().front().operations;
    assert(std::ranges::any_of(
        operations,
        [](const runtime::simir::Operation& operation) {
            return runtime::simir::operation_get_if<
                       runtime::simir::RandomValue>(&operation)
                != nullptr;
        }));
    assert(std::ranges::any_of(
        operations,
        [](const runtime::simir::Operation& operation) {
            return runtime::simir::operation_get_if<
                       runtime::simir::ScopeRandomize>(&operation)
                != nullptr;
        }));
    std::size_t system_commands { };
    bool saw_function { };
    bool saw_task { };
    for (const auto& operation : operations) {
        const auto* command = runtime::simir::operation_get_if<
            runtime::simir::SystemCommand>(&operation);
        if (command == nullptr) {
            continue;
        }
        ++system_commands;
        saw_function = saw_function || command->destination.has_value();
        saw_task = saw_task || !command->destination.has_value();
    }
    assert(system_commands == 2U && saw_function && saw_task);
    assert(std::ranges::any_of(
        operations,
        [](const runtime::simir::Operation& operation) {
            const auto* binary = runtime::simir::operation_get_if<
                runtime::simir::Binary>(&operation);
            return binary != nullptr
                && binary->operation
                    == runtime::simir::BinaryOperator::case_equal;
        }));
}

void run_systemverilog_generate_identity_assignment_test()
{
    auto fixture = systemverilog_hir_fixture(true);
    auto& hir = fixture.compiled.systemverilog_hir;
    auto& unit = hir.mutable_units().front();
    auto& process = hir.mutable_processes().front();
    auto& assignment = hir.mutable_statements().front();
    const auto scope = unit.scope;
    const auto source = assignment.source;
    const auto origin = assignment.origin;

    const auto lane = add_systemverilog_expression(
        fixture, semantic::sv::ExpressionKind::name, "lane");
    const auto zero = add_systemverilog_expression(
        fixture, semantic::sv::ExpressionKind::integer_literal, "0");
    const auto one = add_systemverilog_expression(
        fixture, semantic::sv::ExpressionKind::integer_literal, "1");
    const auto two = add_systemverilog_expression(
        fixture, semantic::sv::ExpressionKind::integer_literal, "2");
    const auto condition = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::binary,
        "<",
        { lane, two });
    const auto iteration = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::binary,
        "+",
        { lane, one });
    assignment.value = lane;

    // Each generated iteration is a distinct process. Keep the identity test
    // legal by assigning its folded iterator to process-local storage instead
    // of making both cloned initial processes drive the module output.
    const auto iteration_value = fixture.compiled.semantics.add_declaration(
        scope,
        semantic::DeclarationKind::variable,
        "iteration_value",
        source,
        origin);
    semantic::sv::Declaration iteration_value_record;
    iteration_value_record.id = iteration_value;
    iteration_value_record.scope = scope;
    iteration_value_record.form = semantic::sv::DeclarationForm::variable;
    iteration_value_record.name = "iteration_value";
    iteration_value_record.source = source;
    iteration_value_record.origin = origin;
    iteration_value_record.type.emplace();
    iteration_value_record.type->target.source = source;
    iteration_value_record.type->target.spelling = "int";
    iteration_value_record.type->executable_width = 32U;
    iteration_value_record.type->signed_value = true;
    hir.mutable_declarations().push_back(
        std::move(iteration_value_record));
    process.declarations.push_back(iteration_value);
    const auto target = std::ranges::find(
        hir.mutable_expressions(), *assignment.target,
        &semantic::sv::Expression::id);
    assert(target != hir.mutable_expressions().end());
    target->text = "iteration_value";
    target->referenced_name->spelling = "iteration_value";
    target->referenced_name->selected = iteration_value;

    const auto declaration = fixture.compiled.semantics.add_declaration(
        scope,
        semantic::DeclarationKind::generate,
        "lanes",
        source,
        origin);
    semantic::sv::Declaration declaration_record;
    declaration_record.id = declaration;
    declaration_record.scope = scope;
    declaration_record.form = semantic::sv::DeclarationForm::generated;
    declaration_record.name = "lanes";
    declaration_record.source = source;
    declaration_record.origin = origin;
    hir.mutable_declarations().push_back(std::move(declaration_record));

    semantic::sv::GenerateRegion generate;
    generate.declaration = declaration;
    generate.scope = scope;
    generate.kind = semantic::sv::GenerateKind::iterative;
    generate.label = "lanes";
    generate.iterator = "lane";
    generate.initial = zero;
    generate.condition = condition;
    generate.iteration = iteration;
    generate.processes.push_back(process.id);
    generate.source = source;
    generate.origin = origin;
    unit.processes.clear();
    unit.generates.push_back(std::move(generate));
    assert(fixture.compiled.valid());

    const auto result = elaborate_compiled(
        fixture.compiled, "sv:work.hir_direct", "hir_direct");
    assert(result.ok() && result.design);
    assert(result.design->processes().size() == 2U);
    for (const auto& generated : result.design->processes()) {
        assert(std::ranges::any_of(
            generated.operations,
            [](const runtime::simir::Operation& operation) {
                return runtime::simir::operation_get_if<
                           runtime::simir::LoadConstant>(&operation)
                    != nullptr;
            }));
    }
}

void run_systemverilog_hir_recursive_function_test()
{
    auto fixture = systemverilog_hir_fixture(true);
    const auto process = fixture.compiled.find_process(fixture.process);
    assert(process && process->systemverilog != nullptr);
    const auto unit = fixture.compiled.systemverilog_units().front().id;
    const auto unit_scope = fixture.compiled.systemverilog_units().front().scope;
    const auto source = process->systemverilog->source;
    const auto origin = process->systemverilog->origin;
    const auto function_scope = fixture.compiled.semantics.add_scope(
        unit, unit_scope, "recursive_bit", source, origin);

    const auto function = fixture.compiled.semantics.add_declaration(
        unit_scope,
        semantic::DeclarationKind::function,
        "recursive_bit",
        source,
        origin);
    const auto formal = fixture.compiled.semantics.add_declaration(
        function_scope,
        semantic::DeclarationKind::port,
        "again",
        source,
        origin);
    semantic::sv::Declaration formal_record;
    formal_record.id = formal;
    formal_record.scope = function_scope;
    formal_record.form = semantic::sv::DeclarationForm::port;
    formal_record.name = "again";
    formal_record.source = source;
    formal_record.origin = origin;
    formal_record.direction = semantic::sv::Direction::input;
    formal_record.type.emplace();
    formal_record.type->target.source = source;
    formal_record.type->target.spelling = "bit";
    formal_record.type->executable_width = 1U;
    fixture.compiled.systemverilog_hir.mutable_declarations().push_back(
        formal_record);

    const auto add_expression = [&](const semantic::sv::ExpressionKind kind,
                                    const std::string_view text,
                                    std::vector<semantic::ExpressionId> operands,
                                    const std::optional<semantic::DeclarationId>
                                        selected = std::nullopt) {
        const auto id = fixture.compiled.semantics.add_expression_identity(
            function_scope, source, origin);
        semantic::sv::Expression expression;
        expression.id = id;
        expression.scope = function_scope;
        expression.kind = kind;
        expression.text = text;
        expression.source = source;
        expression.origin = origin;
        expression.operands = std::move(operands);
        if (selected) {
            expression.referenced_name.emplace();
            expression.referenced_name->spelling = std::string { text };
            expression.referenced_name->source = source;
            expression.referenced_name->selected = *selected;
        }
        fixture.compiled.systemverilog_hir.mutable_expressions().push_back(
            std::move(expression));
        return id;
    };
    const auto again = add_expression(
        semantic::sv::ExpressionKind::name, "again", { }, formal);
    const auto zero = add_expression(
        semantic::sv::ExpressionKind::integer_literal, "1'b0", { });
    const auto recursive_call = add_expression(
        semantic::sv::ExpressionKind::call,
        "recursive_bit",
        { zero },
        function);
    const auto one = add_expression(
        semantic::sv::ExpressionKind::integer_literal, "1'b1", { });

    const auto add_return = [&](const semantic::ExpressionId value) {
        const auto id = fixture.compiled.semantics.add_statement_identity(
            function_scope, source, origin);
        semantic::sv::Statement statement;
        statement.id = id;
        statement.scope = function_scope;
        statement.kind = semantic::sv::StatementKind::return_statement;
        statement.source = source;
        statement.origin = origin;
        statement.value = value;
        fixture.compiled.systemverilog_hir.mutable_statements().push_back(
            std::move(statement));
        return id;
    };
    const auto recursive_return = add_return(recursive_call);
    const auto base_return = add_return(one);
    const auto conditional
        = fixture.compiled.semantics.add_statement_identity(
            function_scope, source, origin);
    semantic::sv::Statement conditional_record;
    conditional_record.id = conditional;
    conditional_record.scope = function_scope;
    conditional_record.kind = semantic::sv::StatementKind::conditional;
    conditional_record.source = source;
    conditional_record.origin = origin;
    conditional_record.condition = again;
    conditional_record.statements.push_back(recursive_return);
    fixture.compiled.systemverilog_hir.mutable_statements().push_back(
        std::move(conditional_record));

    semantic::sv::Declaration function_record;
    function_record.id = function;
    function_record.scope = unit_scope;
    function_record.form = semantic::sv::DeclarationForm::function;
    function_record.name = "recursive_bit";
    function_record.source = source;
    function_record.origin = origin;
    function_record.nested_scope = function_scope;
    function_record.type = formal_record.type;
    function_record.callable.emplace();
    function_record.callable->function = true;
    function_record.callable->return_type = *function_record.type;
    function_record.callable->formals.push_back(formal);
    function_record.callable->lifetime = semantic::sv::Lifetime::automatic;
    function_record.children.push_back(formal);
    function_record.statements = { conditional, base_return };
    fixture.compiled.systemverilog_hir.mutable_declarations().push_back(
        std::move(function_record));
    fixture.compiled.systemverilog_hir.mutable_units().front().declarations
        .push_back(function);

    auto top_call = *fixture.compiled.find_expression(
        fixture.literal)->systemverilog;
    top_call.id = fixture.compiled.semantics.add_expression_identity(
        unit_scope, source, origin);
    top_call.kind = semantic::sv::ExpressionKind::call;
    top_call.text = "recursive_bit";
    top_call.operands = { fixture.literal };
    top_call.referenced_name.emplace();
    top_call.referenced_name->spelling = "recursive_bit";
    top_call.referenced_name->source = source;
    top_call.referenced_name->selected = function;
    fixture.compiled.systemverilog_hir.mutable_expressions().push_back(
        top_call);
    fixture.compiled.systemverilog_hir.mutable_statements().front().value
        = top_call.id;

    assert(fixture.compiled.valid());
    auto result = elaborate_compiled(
        fixture.compiled,
        "sv:work.hir_direct",
        "hir_direct");
    assert(result.ok() && result.design);
    const auto& operations = result.design->processes().front().operations;
    const auto operation_count = [&]<typename Operation>() {
        return std::ranges::count_if(
            operations,
            [](const runtime::simir::Operation& operation) {
                return runtime::simir::operation_get_if<Operation>(
                           &operation)
                    != nullptr;
            });
    };
    assert(operation_count.operator()<runtime::simir::Call>() == 2);
    assert(operation_count.operator()<runtime::simir::Return>() == 1);
    assert(operation_count.operator()<runtime::simir::CallableFramePush>()
        == 2);
    assert(operation_count.operator()<runtime::simir::CallableFramePop>()
        == 2);
    const auto q = result.design->find_signal("q");
    assert(q);
    auto interpreter = result.design->create_interpreter();
    interpreter->start();
    (void)interpreter->run();
    assert(interpreter->signal_value(*q).to_msb_string() == "1");
}

void run_residual_overlay_boundary_test()
{
    auto fixture = systemverilog_hir_fixture(true, true);
    auto result = elaborate_compiled(
        fixture.compiled,
        "sv:work.hir_direct", "hir_direct");
    assert(result.ok() && result.design);
    const auto q = result.design->find_signal("q");
    assert(q);
    auto interpreter = result.design->create_interpreter();
    interpreter->start();
    (void)interpreter->run();
    assert(interpreter->signal_value(*q).to_msb_string() == "1");
}

void require_systemverilog_hir_rejection(
    SystemVerilogHirFixture fixture,
    const std::string_view diagnostic = "FSIM-ELAB-HIR-001")
{
    assert(fixture.compiled.valid());
    const auto rejected = elaborate_compiled(
        fixture.compiled,
        "sv:work.hir_direct", "hir_direct");
    assert(has_diagnostic(rejected, diagnostic));
}

void require_systemverilog_hir_execution(
    SystemVerilogHirFixture fixture,
    const std::string_view expected)
{
    assert(fixture.compiled.valid());
    auto result = elaborate_compiled(
        fixture.compiled,
        "sv:work.hir_direct", "hir_direct");
    assert(result.ok() && result.design);
    const auto q = result.design->find_signal("q");
    assert(q);
    auto interpreter = result.design->create_interpreter();
    interpreter->start();
    assert(interpreter->run().status
        == runtime::RunStatus::completed);
    assert(interpreter->signal_value(*q).to_msb_string()
        == expected);
}

void run_conservative_expression_boundary_tests()
{
    auto unsized = systemverilog_hir_fixture(true);
    const auto literal = std::ranges::find(
        unsized.compiled.systemverilog_hir.mutable_expressions(),
        unsized.literal,
        &semantic::sv::Expression::id);
    assert(literal
        != unsized.compiled.systemverilog_hir.mutable_expressions().end());
    literal->text = "1x";
    require_systemverilog_hir_rejection(std::move(unsized));

    auto arithmetic = systemverilog_hir_fixture(true);
    const auto source = arithmetic.compiled.find_expression(
        arithmetic.literal);
    assert(source && source->systemverilog != nullptr);
    auto binary = *source->systemverilog;
    binary.id = arithmetic.compiled.semantics.add_expression_identity(
        binary.scope, binary.source, binary.origin);
    binary.kind = semantic::sv::ExpressionKind::binary;
    binary.text = "**";
    binary.operands = { arithmetic.literal, arithmetic.literal };
    arithmetic.compiled.systemverilog_hir.mutable_expressions().push_back(
        binary);
    arithmetic.compiled.systemverilog_hir.mutable_statements().front().value
        = binary.id;
    require_systemverilog_hir_execution(
        std::move(arithmetic), "1");
}

void run_composite_expression_test()
{
    auto fixture = systemverilog_hir_fixture(true);
    const auto zero = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::integer_literal,
        "1'b0");
    const auto index_zero = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::integer_literal,
        "32'd0");
    const auto index_one = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::integer_literal,
        "32'd1");
    const auto count = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::integer_literal,
        "32'd2");
    const auto concatenation = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::concatenation,
        "{}",
        { fixture.literal, zero });
    const auto condition = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::index,
        "index",
        { concatenation, index_one });
    const auto slice = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::slice,
        ":",
        { concatenation, index_one, index_zero });
    const auto replication = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::replication,
        "replication",
        { count, zero });
    const auto conditional = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::call,
        "?:",
        { condition, slice, replication });
    fixture.compiled.systemverilog_hir.mutable_statements().front().value
        = conditional;
    assert(fixture.compiled.valid());
    auto result = elaborate_compiled(
        fixture.compiled, "sv:work.hir_direct", "hir_direct");
    assert(result.ok() && result.design);
    const auto& operations = result.design->processes().front().operations;
    assert(std::ranges::any_of(
        operations,
        [](const runtime::simir::Operation& operation) {
            return runtime::simir::operation_get_if<
                       runtime::simir::Extract>(&operation)
                != nullptr;
        }));
    assert(std::ranges::any_of(
        operations,
        [](const runtime::simir::Operation& operation) {
            return runtime::simir::operation_get_if<
                       runtime::simir::Concatenate>(&operation)
                != nullptr;
        }));
    assert(std::ranges::any_of(
        operations,
        [](const runtime::simir::Operation& operation) {
            return runtime::simir::operation_get_if<
                       runtime::simir::ConditionalSelect>(&operation)
                != nullptr;
        }));
    const auto q = result.design->find_signal("q");
    assert(q);
    auto interpreter = result.design->create_interpreter();
    interpreter->start();
    (void)interpreter->run();
    assert(interpreter->signal_value(*q).to_msb_string() == "0");

    auto excessive = systemverilog_hir_fixture(true);
    const auto huge_count = add_systemverilog_expression(
        excessive,
        semantic::sv::ExpressionKind::integer_literal,
        "64'd4294967296");
    const auto huge_replication = add_systemverilog_expression(
        excessive,
        semantic::sv::ExpressionKind::replication,
        "replication",
        { huge_count, excessive.literal });
    excessive.compiled.systemverilog_hir.mutable_statements().front().value
        = huge_replication;
    require_systemverilog_hir_rejection(std::move(excessive));
}

void run_dynamic_packed_selection_test()
{
    auto fixture = systemverilog_hir_fixture(true);
    const auto process = fixture.compiled.find_process(fixture.process);
    assert(process && process->systemverilog != nullptr);
    const auto scope = process->systemverilog->scope;
    const auto source = process->systemverilog->source;
    const auto origin = process->systemverilog->origin;
    const auto initial_statement = fixture.compiled.find_statement(
        process->systemverilog->statements.front());
    assert(initial_statement && initial_statement->systemverilog != nullptr);
    const auto q = *initial_statement->systemverilog->target;

    const auto source_initializer = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::integer_literal,
        "4'b1010");
    const auto slot_initializer = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::integer_literal,
        "32'd0");
    const auto zero = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::integer_literal,
        "1'b0");
    const auto pair = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::integer_literal,
        "2'b11");
    const auto pair_width = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::integer_literal,
        "32'd2");

    const auto add_local = [&](const std::string_view name,
                               const std::uint64_t width,
                               const semantic::ExpressionId initializer,
                               const bool packed) {
        const auto declaration = fixture.compiled.semantics.add_declaration(
            scope,
            semantic::DeclarationKind::variable,
            std::string { name },
            source,
            origin);
        semantic::sv::Declaration record;
        record.id = declaration;
        record.scope = scope;
        record.form = semantic::sv::DeclarationForm::variable;
        record.name = name;
        record.source = source;
        record.origin = origin;
        record.initializer = initializer;
        record.type.emplace();
        record.type->target.source = source;
        record.type->target.spelling = "logic";
        record.type->executable_width = width;
        record.type->four_state = true;
        if (packed) {
            record.type->packed_range.emplace();
            record.type->packed_range->left =
                static_cast<std::int64_t>(width - 1U);
            record.type->packed_range->right = 0;
            record.type->packed_range->descending = true;
            record.type->packed_range->source = source;
        }
        fixture.compiled.systemverilog_hir.mutable_declarations().push_back(
            std::move(record));
        fixture.compiled.systemverilog_hir.mutable_processes().front()
            .declarations.push_back(declaration);
        return declaration;
    };
    const auto packed_value = add_local(
        "packed_value", 4U, source_initializer, true);
    const auto slot = add_local("slot", 32U, slot_initializer, false);

    const auto add_name = [&](const semantic::DeclarationId declaration,
                              const std::string_view name) {
        const auto id = fixture.compiled.semantics.add_expression_identity(
            scope, source, origin);
        semantic::sv::Expression expression;
        expression.id = id;
        expression.scope = scope;
        expression.kind = semantic::sv::ExpressionKind::name;
        expression.text = name;
        expression.source = source;
        expression.origin = origin;
        expression.referenced_name.emplace();
        expression.referenced_name->spelling = name;
        expression.referenced_name->source = source;
        expression.referenced_name->selected = declaration;
        fixture.compiled.systemverilog_hir.mutable_expressions().push_back(
            std::move(expression));
        return id;
    };
    const auto packed_name = add_name(packed_value, "packed_value");
    const auto slot_name = add_name(slot, "slot");
    const auto dynamic_bit = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::index,
        "index",
        { packed_name, slot_name });
    const auto dynamic_part = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::slice,
        "+:",
        { packed_name, slot_name, pair_width });
    const auto dynamic_signal_bit = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::index,
        "index",
        { q, slot_name });

    const auto bit_write = clone_systemverilog_assignment(fixture);
    const auto part_write = clone_systemverilog_assignment(fixture);
    const auto bit_read = clone_systemverilog_assignment(fixture);
    const auto part_read = clone_systemverilog_assignment(fixture);
    const auto signal_write = clone_systemverilog_assignment(fixture);
    const auto set_assignment = [&](const semantic::StatementId statement,
                                    const semantic::ExpressionId target,
                                    const semantic::ExpressionId value) {
        const auto found = std::ranges::find(
            fixture.compiled.systemverilog_hir.mutable_statements(),
            statement,
            &semantic::sv::Statement::id);
        assert(found
            != fixture.compiled.systemverilog_hir.mutable_statements().end());
        found->target = target;
        found->value = value;
    };
    set_assignment(bit_write, dynamic_bit, zero);
    set_assignment(part_write, dynamic_part, pair);
    set_assignment(bit_read, q, dynamic_bit);
    set_assignment(part_read, q, dynamic_part);
    set_assignment(signal_write, dynamic_signal_bit, fixture.literal);
    fixture.compiled.systemverilog_hir.mutable_processes().front().statements
        = { bit_write, part_write, bit_read, part_read, signal_write };
    assert(fixture.compiled.valid());

    auto result = elaborate_compiled(
        fixture.compiled,
        "sv:work.hir_direct",
        "hir_direct");
    assert(result.ok() && result.design);
    const auto& operations = result.design->processes().front().operations;
    const auto has_operation = [&]<typename Operation>() {
        return std::ranges::any_of(
            operations,
            [](const runtime::simir::Operation& operation) {
                return runtime::simir::operation_get_if<Operation>(
                           &operation)
                    != nullptr;
            });
    };
    assert(has_operation.operator()<runtime::simir::DynamicExtract>());
    assert(has_operation.operator()<runtime::simir::DynamicPartSelect>());
    assert(has_operation.operator()<runtime::simir::DynamicInsert>());
    assert(has_operation.operator()<runtime::simir::DynamicPartInsert>());
    assert(has_operation.operator()<
        runtime::simir::WriteBlockingDynamicSlice>());
    assert(std::ranges::all_of(
        result.design->processes().front().expression_profiles,
        [](const runtime::simir::ExpressionProfile& profile) {
            return profile.source.path == "hir_direct.sv";
        }));
    const auto output = result.design->find_signal("q");
    assert(output);
    auto interpreter = result.design->create_interpreter();
    interpreter->start();
    (void)interpreter->run();
    assert(interpreter->signal_value(*output).to_msb_string() == "1");

    auto unsupported = systemverilog_hir_fixture(true);
    const auto runtime_width = add_systemverilog_expression(
        unsupported,
        semantic::sv::ExpressionKind::name,
        "q");
    auto& runtime_width_expression =
        unsupported.compiled.systemverilog_hir.mutable_expressions().back();
    const auto q_declaration = unsupported.compiled.systemverilog_hir
                                   .mutable_units()
                                   .front()
                                   .declarations.back();
    runtime_width_expression.referenced_name.emplace();
    runtime_width_expression.referenced_name->spelling = "q";
    runtime_width_expression.referenced_name->source
        = runtime_width_expression.source;
    runtime_width_expression.referenced_name->selected = q_declaration;
    const auto malformed_part = add_systemverilog_expression(
        unsupported,
        semantic::sv::ExpressionKind::slice,
        "+:",
        { runtime_width, runtime_width, runtime_width });
    unsupported.compiled.systemverilog_hir.mutable_statements().front().value
        = malformed_part;
    const auto rejected = elaborate_compiled(
        unsupported.compiled,
        "sv:work.hir_direct",
        "hir_direct");
    assert(has_diagnostic(rejected, "FSIM-ELAB-HIR-001"));
}

void run_statically_bounded_loop_test()
{
    auto fixture = systemverilog_hir_fixture(true);
    const auto zero = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::integer_literal,
        "32'd0");
    const auto one = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::integer_literal,
        "32'd1");
    const auto two = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::integer_literal,
        "32'd2");
    const auto false_value = add_systemverilog_expression(
        fixture,
        semantic::sv::ExpressionKind::logic_literal,
        "1'b0");
    const auto repeat_body = clone_systemverilog_assignment(fixture);
    const auto for_body = clone_systemverilog_assignment(fixture);
    const auto while_body = clone_systemverilog_assignment(fixture);
    const auto do_while_body = clone_systemverilog_assignment(fixture);
    const auto repeat = add_systemverilog_loop(
        fixture, zero, two, repeat_body, true);
    const auto bounded_for = add_systemverilog_loop(
        fixture, zero, one, for_body, false);
    const auto while_false = add_systemverilog_loop(
        fixture, zero, false_value, while_body, false, true);
    const auto do_while_false = add_systemverilog_loop(
        fixture, zero, false_value, do_while_body, false, true, true);
    fixture.compiled.systemverilog_hir.mutable_processes().front().statements
        = { repeat, bounded_for, while_false, do_while_false };
    assert(fixture.compiled.valid());
    auto result = elaborate_compiled(
        fixture.compiled, "sv:work.hir_direct", "hir_direct");
    assert(result.ok() && result.design);
    const auto write_count = std::ranges::count_if(
               result.design->processes().front().operations,
               [](const runtime::simir::Operation& operation) {
                   return runtime::simir::operation_get_if<
                              runtime::simir::WriteBlocking>(&operation)
                       != nullptr;
               });
    assert(write_count == 5);

    auto excessive = systemverilog_hir_fixture(true);
    const auto excessive_zero = add_systemverilog_expression(
        excessive,
        semantic::sv::ExpressionKind::integer_literal,
        "32'd0");
    const auto excessive_count = add_systemverilog_expression(
        excessive,
        semantic::sv::ExpressionKind::integer_literal,
        "32'd1000001");
    const auto excessive_body = clone_systemverilog_assignment(excessive);
    const auto excessive_repeat = add_systemverilog_loop(
        excessive,
        excessive_zero,
        excessive_count,
        excessive_body,
        true);
    excessive.compiled.systemverilog_hir.mutable_processes().front().statements
        = { excessive_repeat };
    require_systemverilog_hir_rejection(
        std::move(excessive), "FSIM-ELAB-073");
}

void run_local_nonblocking_boundary_test()
{
    auto fixture = systemverilog_hir_fixture(true);
    const auto process = fixture.compiled.find_process(fixture.process);
    assert(process && process->systemverilog != nullptr);
    const auto& process_record = *process->systemverilog;
    const auto local = fixture.compiled.semantics.add_declaration(
        process_record.scope,
        semantic::DeclarationKind::variable,
        "local_value",
        process_record.source,
        process_record.origin);
    semantic::sv::Declaration local_declaration;
    local_declaration.id = local;
    local_declaration.scope = process_record.scope;
    local_declaration.form = semantic::sv::DeclarationForm::variable;
    local_declaration.name = "local_value";
    local_declaration.source = process_record.source;
    local_declaration.origin = process_record.origin;
    local_declaration.type.emplace();
    local_declaration.type->target.source = process_record.source;
    local_declaration.type->target.spelling = "logic";
    local_declaration.type->executable_width = 1U;
    local_declaration.type->four_state = true;
    fixture.compiled.systemverilog_hir.mutable_declarations().push_back(
        std::move(local_declaration));
    fixture.compiled.systemverilog_hir.mutable_processes().front()
        .declarations.push_back(local);

    const auto target = fixture.compiled.semantics.add_expression_identity(
        process_record.scope,
        process_record.source,
        process_record.origin);
    semantic::sv::Expression target_expression;
    target_expression.id = target;
    target_expression.scope = process_record.scope;
    target_expression.kind = semantic::sv::ExpressionKind::name;
    target_expression.text = "local_value";
    target_expression.source = process_record.source;
    target_expression.origin = process_record.origin;
    target_expression.referenced_name.emplace();
    target_expression.referenced_name->spelling = "local_value";
    target_expression.referenced_name->source = process_record.source;
    target_expression.referenced_name->selected = local;
    fixture.compiled.systemverilog_hir.mutable_expressions().push_back(
        std::move(target_expression));
    auto& statement
        = fixture.compiled.systemverilog_hir.mutable_statements().front();
    statement.target = target;
    statement.assignment_kind
        = semantic::sv::AssignmentKind::nonblocking;
    require_systemverilog_hir_rejection(std::move(fixture));
}

void run_systemverilog_equality_test()
{
    auto fixture = systemverilog_hir_fixture(true);
    auto literal = fixture.compiled.find_expression(fixture.literal);
    assert(literal && literal->systemverilog != nullptr);
    auto changed_literal = *literal->systemverilog;
    changed_literal.text = "1'bx";
    const auto found_literal = std::ranges::find(
        fixture.compiled.systemverilog_hir.mutable_expressions(),
        fixture.literal,
        &semantic::sv::Expression::id);
    assert(found_literal
        != fixture.compiled.systemverilog_hir.mutable_expressions().end());
    *found_literal = changed_literal;

    auto equality = changed_literal;
    equality.id = fixture.compiled.semantics.add_expression_identity(
        equality.scope, equality.source, equality.origin);
    equality.kind = semantic::sv::ExpressionKind::binary;
    equality.text = "===";
    equality.operands = { fixture.literal, fixture.literal };
    fixture.compiled.systemverilog_hir.mutable_expressions().push_back(
        equality);
    fixture.compiled.systemverilog_hir.mutable_statements().front().value
        = equality.id;
    assert(fixture.compiled.valid());
    auto result = elaborate_compiled(
        fixture.compiled, "sv:work.hir_direct", "hir_direct");
    assert(result.ok() && result.design);
    const auto q = result.design->find_signal("q");
    assert(q);
    assert(std::ranges::any_of(
        result.design->processes().front().operations,
        [](const runtime::simir::Operation& operation) {
            const auto* binary = runtime::simir::operation_get_if<
                runtime::simir::Binary>(&operation);
            return binary != nullptr
                && binary->operation
                    == runtime::simir::BinaryOperator::case_equal;
        }));
    auto interpreter = result.design->create_interpreter();
    interpreter->start();
    (void)interpreter->run();
    assert(interpreter->signal_value(*q).to_msb_string() == "1");
}

void run_unavailable_hir_boundary_test()
{
    auto unavailable = systemverilog_hir_fixture(false);
    const auto unavailable_result = elaborate_compiled(
        unavailable.compiled,
        "sv:work.hir_direct", "hir_direct");
    assert(has_diagnostic(
        unavailable_result, "FSIM-ELAB-SVEVENT-005"));
}

void run_vhdl_direct_hir_test()
{
    const auto parsed = frontend::parse_text(
        "hir_direct.vhd",
        R"(
entity hir_direct_vhdl is
  port (a : in bit; q : out bit);
end entity;
architecture rtl of hir_direct_vhdl is
begin
  drive : process(a)
  begin
    q <= a;
  end process;
end architecture;
)",
        frontend::Language::Vhdl2008);
    assert(parsed.ok());
    auto adapter = parsed.design;
    const auto entity_adapter = std::ranges::find_if(
        adapter.units,
        [](const frontend::DesignUnit& unit) {
            return unit.kind == frontend::UnitKind::VhdlEntity;
        });
    const auto architecture_adapter = std::ranges::find_if(
        adapter.units,
        [](const frontend::DesignUnit& unit) {
            return unit.kind == frontend::UnitKind::VhdlArchitecture;
        });
    assert(entity_adapter != adapter.units.end());
    assert(architecture_adapter != adapter.units.end());
    assert(architecture_adapter->processes.size() == 1U);
    const auto process_adapter = architecture_adapter->processes.front();
    assert(process_adapter.statements.size() == 1U);

    semantic::Model model;
    semantic::sv::Hir systemverilog;
    semantic::vhdl::Hir vhdl;
    const auto file = model.intern_source_file(
        "hir_direct.vhd", "hir-direct-vhdl-digest");
    const auto entity_source = add_source_span(
        model, file, entity_adapter->span);
    const auto architecture_source = add_source_span(
        model, file, architecture_adapter->span);
    const auto process_source = add_source_span(
        model, file, process_adapter.span);
    const auto statement_source = add_source_span(
        model, file, process_adapter.statements.front().span);
    const auto entity_origin = model.add_origin(
        semantic::OriginKind::parsed, entity_source);
    const auto architecture_origin = model.add_origin(
        semantic::OriginKind::parsed, architecture_source);
    const auto entity = model.add_unit(
        semantic::Language::vhdl,
        semantic::UnitKind::vhdl_entity,
        "work",
        "hir_direct_vhdl",
        { },
        entity_source,
        entity_origin);
    const auto architecture = model.add_unit(
        semantic::Language::vhdl,
        semantic::UnitKind::vhdl_architecture,
        "work",
        "rtl",
        "hir_direct_vhdl",
        architecture_source,
        architecture_origin);
    const auto entity_scope = model.units()[entity.value()].scope;
    const auto architecture_scope = model.units()[architecture.value()].scope;
    const auto process_scope = model.add_scope(
        architecture,
        architecture_scope,
        process_adapter.name,
        process_source,
        architecture_origin);

    const auto add_port = [&](const std::string& name,
                              const semantic::vhdl::Direction direction) {
        const auto id = model.add_declaration(
            entity_scope,
            semantic::DeclarationKind::port,
            name,
            entity_source,
            entity_origin);
        semantic::vhdl::Declaration declaration;
        declaration.id = id;
        declaration.scope = entity_scope;
        declaration.form = semantic::vhdl::DeclarationForm::port;
        declaration.name = name;
        declaration.source = entity_source;
        declaration.origin = entity_origin;
        declaration.object_class = semantic::vhdl::ObjectClass::signal;
        declaration.direction = direction;
        declaration.subtype.emplace();
        declaration.subtype->type_mark.source = entity_source;
        declaration.subtype->type_mark.spelling = "bit";
        declaration.subtype->domain = semantic::vhdl::ValueDomain::bit2;
        declaration.subtype->executable_width = 1U;
        vhdl.mutable_declarations().push_back(std::move(declaration));
        return id;
    };
    const auto a = add_port("a", semantic::vhdl::Direction::input);
    const auto q = add_port("q", semantic::vhdl::Direction::output);

    const auto add_name = [&](const std::string& name,
                              const semantic::DeclarationId selected) {
        const auto id = model.add_expression_identity(
            process_scope, statement_source, architecture_origin);
        semantic::vhdl::Expression expression;
        expression.id = id;
        expression.scope = process_scope;
        expression.kind = semantic::vhdl::ExpressionKind::name;
        expression.text = name;
        expression.source = statement_source;
        expression.origin = architecture_origin;
        expression.referenced_name.emplace();
        expression.referenced_name->spelling = name;
        expression.referenced_name->canonical = name;
        expression.referenced_name->source = statement_source;
        expression.referenced_name->selected = selected;
        vhdl.mutable_expressions().push_back(std::move(expression));
        return id;
    };
    const auto target = add_name("q", q);
    const auto value = add_name("a", a);
    const auto statement = model.add_statement_identity(
        process_scope, statement_source, architecture_origin);
    semantic::vhdl::Statement statement_record;
    statement_record.id = statement;
    statement_record.scope = process_scope;
    statement_record.kind = semantic::vhdl::StatementKind::signal_assignment;
    statement_record.source = statement_source;
    statement_record.origin = architecture_origin;
    statement_record.target = target;
    statement_record.value = value;
    vhdl.mutable_statements().push_back(statement_record);

    const auto process = model.add_process_identity(
        process_scope,
        process_adapter.name,
        process_source,
        architecture_origin);
    semantic::vhdl::Process process_record;
    process_record.id = process;
    process_record.scope = process_scope;
    process_record.name = process_adapter.name;
    process_record.source = process_source;
    process_record.origin = architecture_origin;
    semantic::vhdl::Sensitivity sensitivity;
    sensitivity.signal = "a";
    sensitivity.source = process_source;
    process_record.sensitivities.push_back(std::move(sensitivity));
    process_record.statements.push_back(statement);
    vhdl.mutable_processes().push_back(process_record);

    semantic::vhdl::Unit entity_record;
    entity_record.id = entity;
    entity_record.scope = entity_scope;
    entity_record.kind = semantic::vhdl::UnitKind::entity;
    entity_record.library = "work";
    entity_record.name = "hir_direct_vhdl";
    entity_record.source = entity_source;
    entity_record.origin = entity_origin;
    entity_record.declarations = { a, q };
    vhdl.mutable_units().push_back(std::move(entity_record));
    semantic::vhdl::Unit architecture_record;
    architecture_record.id = architecture;
    architecture_record.scope = architecture_scope;
    architecture_record.kind = semantic::vhdl::UnitKind::architecture;
    architecture_record.library = "work";
    architecture_record.name = "rtl";
    architecture_record.primary_name = "hir_direct_vhdl";
    architecture_record.source = architecture_source;
    architecture_record.origin = architecture_origin;
    architecture_record.processes.push_back(process);
    vhdl.mutable_units().push_back(std::move(architecture_record));

    semantic::CompiledDesign compiled {
        std::move(model), std::move(systemverilog), std::move(vhdl) };
    assert(compiled.valid());
    const auto require_vhdl_hir_rejection
        = [&](semantic::CompiledDesign variant,
              const std::string_view diagnostic
                  = "FSIM-ELAB-HIR-001") {
        assert(variant.valid());
        const auto rejected = elaborate_compiled(
            variant,
            "vhdl:work.hir_direct_vhdl(rtl)",
            "hir_direct_vhdl");
        assert(has_diagnostic(rejected, diagnostic));
    };
    const auto is_vhdl_signal_write
        = [](const runtime::simir::Operation& operation) {
              return runtime::simir::operation_get_if<
                         runtime::simir::WriteUpdate>(&operation)
                      != nullptr
                  || runtime::simir::operation_get_if<
                         runtime::simir::WriteProjected>(&operation)
                      != nullptr
                  || runtime::simir::operation_get_if<
                         runtime::simir::WriteInertial>(&operation)
                      != nullptr;
          };
    const auto require_vhdl_hir_execution
        = [&](semantic::CompiledDesign variant) {
        assert(variant.valid());
        const auto lowered = elaborate_compiled(
            variant,
            "vhdl:work.hir_direct_vhdl(rtl)",
            "hir_direct_vhdl");
        assert(lowered.ok() && lowered.design);
        assert(std::ranges::any_of(
            lowered.design->processes(),
            [&](const runtime::simir::Process& lowered_process) {
                return std::ranges::any_of(
                    lowered_process.operations, is_vhdl_signal_write);
            }));
    };
    require_vhdl_hir_execution(compiled);
    {
        auto variant = compiled;
        const auto entity_process_scope = variant.semantics.add_scope(
            entity,
            entity_scope,
            "entity_drive",
            process_source,
            entity_origin);
        const auto entity_process = variant.semantics.add_process_identity(
            entity_process_scope,
            "entity_drive",
            process_source,
            entity_origin);
        const auto source_process = variant.find_process(process);
        assert(source_process && source_process->vhdl != nullptr);
        auto entity_process_record = *source_process->vhdl;
        entity_process_record.id = entity_process;
        entity_process_record.scope = entity_process_scope;
        entity_process_record.name = "entity_drive";
        entity_process_record.origin = entity_origin;
        variant.vhdl_hir.mutable_processes().push_back(
            std::move(entity_process_record));
        auto& units = variant.vhdl_hir.mutable_units();
        const auto mutable_entity = std::ranges::find(
            units, entity, &semantic::vhdl::Unit::id);
        const auto mutable_architecture = std::ranges::find(
            units, architecture, &semantic::vhdl::Unit::id);
        assert(mutable_entity != units.end());
        assert(mutable_architecture != units.end());
        mutable_entity->processes = { entity_process };
        mutable_architecture->processes.clear();
        require_vhdl_hir_execution(std::move(variant));
    }
    {
        auto variant = compiled;
        auto& units = variant.vhdl_hir.mutable_units();
        const auto mutable_entity = std::ranges::find(
            units, entity, &semantic::vhdl::Unit::id);
        assert(mutable_entity != units.end());
        mutable_entity->configuration.emplace();
        assert(variant.valid());
        const auto rejected = elaborate_compiled(
            variant,
            "vhdl:work.hir_direct_vhdl(rtl)",
            "hir_direct_vhdl");
        assert(has_diagnostic(
            rejected, "FSIM-ELAB-VHENTITY-001"));
    }
    {
        auto variant = compiled;
        variant.vhdl_hir.mutable_statements().front().delay_mechanism
            = semantic::vhdl::DelayMechanism::transport;
        require_vhdl_hir_execution(std::move(variant));
    }
    {
        auto variant = compiled;
        variant.vhdl_hir.mutable_statements().front().delay_mechanism
            = semantic::vhdl::DelayMechanism::inertial;
        require_vhdl_hir_execution(std::move(variant));
    }
    {
        auto variant = compiled;
        variant.vhdl_hir.mutable_statements().front().postponed = true;
        require_vhdl_hir_execution(std::move(variant));
    }
    {
        auto variant = compiled;
        auto& changed_statement
            = variant.vhdl_hir.mutable_statements().front();
        changed_statement.waveform.push_back({
            *changed_statement.value,
            std::nullopt,
            true,
            changed_statement.source });
        require_vhdl_hir_rejection(
            std::move(variant), "FSIM-ELAB-VHDLGUARD-002");
    }
    {
        auto variant = compiled;
        variant.vhdl_hir.mutable_processes().front().postponed = true;
        require_vhdl_hir_execution(std::move(variant));
    }
    for (const auto kind : {
             semantic::vhdl::StatementKind::exit_loop,
             semantic::vhdl::StatementKind::next_loop }) {
        auto variant = compiled;
        auto& changed_statement
            = variant.vhdl_hir.mutable_statements().front();
        changed_statement.kind = kind;
        changed_statement.loop_control_label = "outer";
        require_vhdl_hir_rejection(
            std::move(variant), "FSIM-ELAB-078");
    }

    auto unselected_compiled = compiled;
    for (auto& expression :
        unselected_compiled.vhdl_hir.mutable_expressions()) {
        if (expression.referenced_name) {
            expression.referenced_name->selected.reset();
        }
    }
    assert(unselected_compiled.valid());
    auto unselected_result = elaborate_compiled(
        unselected_compiled,
        "vhdl:work.hir_direct_vhdl(rtl)",
        "hir_direct_vhdl");
    assert(unselected_result.ok() && unselected_result.design);
    assert(std::ranges::any_of(
        unselected_result.design->processes().front().operations,
        is_vhdl_signal_write));

    auto ambiguous_compiled = unselected_compiled;
    const auto duplicate_q = ambiguous_compiled.semantics.add_declaration(
        entity_scope,
        semantic::DeclarationKind::port,
        "q",
        entity_source,
        entity_origin);
    const auto original_q = ambiguous_compiled.find_declaration(q);
    assert(original_q && original_q->vhdl != nullptr);
    auto duplicate_q_record = *original_q->vhdl;
    duplicate_q_record.id = duplicate_q;
    ambiguous_compiled.vhdl_hir.mutable_declarations().push_back(
        std::move(duplicate_q_record));
    const auto mutable_entity = std::ranges::find(
        ambiguous_compiled.vhdl_hir.mutable_units(),
        entity,
        &semantic::vhdl::Unit::id);
    assert(mutable_entity
        != ambiguous_compiled.vhdl_hir.mutable_units().end());
    mutable_entity->declarations.push_back(duplicate_q);
    assert(ambiguous_compiled.valid());
    const auto ambiguous_result = elaborate_compiled(
        ambiguous_compiled,
        "vhdl:work.hir_direct_vhdl(rtl)",
        "hir_direct_vhdl");
    assert(has_diagnostic(ambiguous_result, "FSIM-ELAB-HIR-001"));

    auto constant_compiled = compiled;
    const auto constant_literal
        = constant_compiled.semantics.add_expression_identity(
            architecture_scope, statement_source, architecture_origin);
    semantic::vhdl::Expression constant_literal_record;
    constant_literal_record.id = constant_literal;
    constant_literal_record.scope = architecture_scope;
    constant_literal_record.kind
        = semantic::vhdl::ExpressionKind::integer_literal;
    constant_literal_record.text = "1";
    constant_literal_record.source = statement_source;
    constant_literal_record.origin = architecture_origin;
    constant_compiled.vhdl_hir.mutable_expressions().push_back(
        std::move(constant_literal_record));
    const auto constant = constant_compiled.semantics.add_declaration(
        architecture_scope,
        semantic::DeclarationKind::constant,
        "next_value",
        architecture_source,
        architecture_origin);
    semantic::vhdl::Declaration constant_record;
    constant_record.id = constant;
    constant_record.scope = architecture_scope;
    constant_record.form = semantic::vhdl::DeclarationForm::constant;
    constant_record.name = "next_value";
    constant_record.source = architecture_source;
    constant_record.origin = architecture_origin;
    constant_record.initializer = constant_literal;
    constant_record.subtype.emplace();
    constant_record.subtype->type_mark.source = architecture_source;
    constant_record.subtype->type_mark.spelling = "bit";
    constant_record.subtype->domain = semantic::vhdl::ValueDomain::bit2;
    constant_record.subtype->executable_width = 1U;
    constant_compiled.vhdl_hir.mutable_declarations().push_back(
        std::move(constant_record));
    const auto constant_name
        = constant_compiled.semantics.add_expression_identity(
            process_scope, statement_source, architecture_origin);
    semantic::vhdl::Expression constant_name_record;
    constant_name_record.id = constant_name;
    constant_name_record.scope = process_scope;
    constant_name_record.kind = semantic::vhdl::ExpressionKind::name;
    constant_name_record.text = "next_value";
    constant_name_record.source = statement_source;
    constant_name_record.origin = architecture_origin;
    constant_name_record.referenced_name.emplace();
    constant_name_record.referenced_name->spelling = "next_value";
    constant_name_record.referenced_name->canonical = "next_value";
    constant_name_record.referenced_name->source = statement_source;
    constant_name_record.referenced_name->selected = constant;
    constant_compiled.vhdl_hir.mutable_expressions().push_back(
        std::move(constant_name_record));
    constant_compiled.vhdl_hir.mutable_statements().front().value
        = constant_name;
    auto& constant_architecture
        = constant_compiled.vhdl_hir.mutable_units().back();
    constant_architecture.declarations.push_back(constant);
    assert(constant_compiled.valid());
    const auto constant_result = elaborate_compiled(
        constant_compiled,
        "vhdl:work.hir_direct_vhdl(rtl)",
        "hir_direct_vhdl");
    assert(constant_result.ok() && constant_result.design);

    auto result = elaborate_compiled(
        compiled,
        "vhdl:work.hir_direct_vhdl(rtl)",
        "hir_direct_vhdl");
    assert(result.ok() && result.design);
    assert(result.design->processes().size() == 1U);
    const auto& lowered_process = result.design->processes().front();
    assert((lowered_process.static_sensitivity
        == std::vector<runtime::simir::Sensitivity> {
            { *result.design->find_signal("a"),
                runtime::simir::EdgeKind::any }
        }));
    assert(std::ranges::any_of(
        lowered_process.operations,
        is_vhdl_signal_write));
    assert(lowered_process.driver_regions.size() == 1U);
    const auto a_signal = result.design->find_signal("a");
    const auto q_signal = result.design->find_signal("q");
    assert(a_signal && q_signal);
    auto interpreter = result.design->create_interpreter();
    interpreter->deposit_signal(
        *a_signal, runtime::PackedLogic4::from_msb_string("1"));
    interpreter->start();
    (void)interpreter->run();
    assert(interpreter->signal_value(*q_signal).to_msb_string() == "1");

    auto callable_compiled = compiled;
    const auto function_scope = callable_compiled.semantics.add_scope(
        architecture,
        architecture_scope,
        "identity",
        process_source,
        architecture_origin);
    const auto function = callable_compiled.semantics.add_declaration(
        architecture_scope,
        semantic::DeclarationKind::function,
        "identity",
        process_source,
        architecture_origin);
    const auto formal = callable_compiled.semantics.add_declaration(
        function_scope,
        semantic::DeclarationKind::port,
        "x",
        process_source,
        architecture_origin);
    semantic::vhdl::SubtypeIndication bit_subtype;
    bit_subtype.type_mark.source = process_source;
    bit_subtype.type_mark.spelling = "bit";
    bit_subtype.domain = semantic::vhdl::ValueDomain::bit2;
    bit_subtype.executable_width = 1U;
    semantic::vhdl::Declaration formal_record;
    formal_record.id = formal;
    formal_record.scope = function_scope;
    formal_record.form = semantic::vhdl::DeclarationForm::port;
    formal_record.name = "x";
    formal_record.source = process_source;
    formal_record.origin = architecture_origin;
    formal_record.object_class = semantic::vhdl::ObjectClass::constant;
    formal_record.direction = semantic::vhdl::Direction::input;
    formal_record.subtype = bit_subtype;
    callable_compiled.vhdl_hir.mutable_declarations().push_back(
        formal_record);

    const auto formal_expression
        = callable_compiled.semantics.add_expression_identity(
            function_scope, statement_source, architecture_origin);
    semantic::vhdl::Expression formal_expression_record;
    formal_expression_record.id = formal_expression;
    formal_expression_record.scope = function_scope;
    formal_expression_record.kind = semantic::vhdl::ExpressionKind::name;
    formal_expression_record.text = "x";
    formal_expression_record.source = statement_source;
    formal_expression_record.origin = architecture_origin;
    formal_expression_record.referenced_name.emplace();
    formal_expression_record.referenced_name->spelling = "x";
    formal_expression_record.referenced_name->canonical = "x";
    formal_expression_record.referenced_name->source = statement_source;
    formal_expression_record.referenced_name->selected = formal;
    callable_compiled.vhdl_hir.mutable_expressions().push_back(
        std::move(formal_expression_record));
    const auto return_statement
        = callable_compiled.semantics.add_statement_identity(
            function_scope, statement_source, architecture_origin);
    semantic::vhdl::Statement return_record;
    return_record.id = return_statement;
    return_record.scope = function_scope;
    return_record.kind = semantic::vhdl::StatementKind::return_statement;
    return_record.source = statement_source;
    return_record.origin = architecture_origin;
    return_record.value = formal_expression;
    callable_compiled.vhdl_hir.mutable_statements().push_back(
        std::move(return_record));

    semantic::vhdl::Declaration function_record;
    function_record.id = function;
    function_record.scope = architecture_scope;
    function_record.form = semantic::vhdl::DeclarationForm::function;
    function_record.name = "identity";
    function_record.source = process_source;
    function_record.origin = architecture_origin;
    function_record.subtype = bit_subtype;
    function_record.nested_scope = function_scope;
    function_record.callable.emplace();
    function_record.callable->function = true;
    function_record.callable->pure = true;
    function_record.callable->defined = true;
    function_record.callable->return_type = bit_subtype;
    function_record.callable->formals.push_back(formal);
    function_record.children.push_back(formal);
    function_record.statements.push_back(return_statement);
    callable_compiled.vhdl_hir.mutable_declarations().push_back(
        std::move(function_record));
    const auto mutable_architecture = std::ranges::find(
        callable_compiled.vhdl_hir.mutable_units(),
        architecture,
        &semantic::vhdl::Unit::id);
    assert(mutable_architecture
        != callable_compiled.vhdl_hir.mutable_units().end());
    mutable_architecture->declarations.push_back(function);

    auto call = *callable_compiled.find_expression(value)->vhdl;
    call.id = callable_compiled.semantics.add_expression_identity(
        process_scope, statement_source, architecture_origin);
    call.kind = semantic::vhdl::ExpressionKind::call;
    call.text = "identity";
    call.operands = { value };
    call.argument_names = { "x" };
    call.referenced_name.emplace();
    call.referenced_name->spelling = "identity";
    call.referenced_name->canonical = "identity";
    call.referenced_name->source = statement_source;
    call.referenced_name->selected = function;
    callable_compiled.vhdl_hir.mutable_expressions().push_back(call);
    callable_compiled.vhdl_hir.mutable_statements().front().value = call.id;
    assert(callable_compiled.valid());
    auto callable_result = elaborate_compiled(
        callable_compiled,
        "vhdl:work.hir_direct_vhdl(rtl)",
        "hir_direct_vhdl");
    assert(callable_result.ok() && callable_result.design);
    const auto& callable_operations
        = callable_result.design->processes().front().operations;
    assert(std::ranges::any_of(
        callable_operations,
        [](const runtime::simir::Operation& operation) {
            return runtime::simir::operation_get_if<runtime::simir::Call>(
                       &operation)
                != nullptr;
        }));
    assert(std::ranges::any_of(
        callable_operations,
        [](const runtime::simir::Operation& operation) {
            return runtime::simir::operation_get_if<runtime::simir::Return>(
                       &operation)
                != nullptr;
        }));
    assert(std::ranges::any_of(
        callable_operations,
        [](const runtime::simir::Operation& operation) {
            return runtime::simir::operation_get_if<
                       runtime::simir::CallableFramePush>(&operation)
                != nullptr;
        }));
    const auto callable_a = callable_result.design->find_signal("a");
    const auto callable_q = callable_result.design->find_signal("q");
    assert(callable_a && callable_q);
    auto callable_interpreter
        = callable_result.design->create_interpreter();
    callable_interpreter->deposit_signal(
        *callable_a, runtime::PackedLogic4::from_msb_string("1"));
    callable_interpreter->start();
    (void)callable_interpreter->run();
    assert(callable_interpreter->signal_value(*callable_q).to_msb_string()
        == "1");

    auto output_formal = callable_compiled;
    const auto changed_formal = std::ranges::find(
        output_formal.vhdl_hir.mutable_declarations(),
        formal,
        &semantic::vhdl::Declaration::id);
    assert(changed_formal
        != output_formal.vhdl_hir.mutable_declarations().end());
    changed_formal->direction = semantic::vhdl::Direction::output;
    const auto unsupported_result = elaborate_compiled(
        output_formal,
        "vhdl:work.hir_direct_vhdl(rtl)",
        "hir_direct_vhdl");
    assert(has_diagnostic(unsupported_result, "FSIM-ELAB-HIR-001"));

    auto equality_compiled = compiled;
    const auto value_expression = equality_compiled.find_expression(value);
    assert(value_expression && value_expression->vhdl != nullptr);
    auto equality = *value_expression->vhdl;
    equality.id = equality_compiled.semantics.add_expression_identity(
        equality.scope, equality.source, equality.origin);
    equality.kind = semantic::vhdl::ExpressionKind::binary;
    equality.text = "=";
    equality.referenced_name.reset();
    equality.operands = { value, value };
    equality_compiled.vhdl_hir.mutable_expressions().push_back(equality);
    equality_compiled.vhdl_hir.mutable_statements().front().value
        = equality.id;
    assert(equality_compiled.valid());
    auto equality_result = elaborate_compiled(
        equality_compiled,
        "vhdl:work.hir_direct_vhdl(rtl)",
        "hir_direct_vhdl");
    assert(equality_result.ok() && equality_result.design);
    assert(std::ranges::any_of(
        equality_result.design->processes().front().operations,
        [](const runtime::simir::Operation& operation) {
            const auto* binary = runtime::simir::operation_get_if<
                runtime::simir::Binary>(&operation);
            return binary != nullptr
                && binary->operation
                    == runtime::simir::BinaryOperator::case_equal;
        }));

    auto conditional_compiled = compiled;
    const auto mutable_a = std::ranges::find(
        conditional_compiled.vhdl_hir.mutable_declarations(),
        a,
        &semantic::vhdl::Declaration::id);
    assert(mutable_a
        != conditional_compiled.vhdl_hir.mutable_declarations().end());
    assert(mutable_a->subtype);
    semantic::vhdl::RangeConstraint bit_range;
    bit_range.left = 0;
    bit_range.right = 0;
    bit_range.descending = true;
    bit_range.source = mutable_a->source;
    mutable_a->subtype->constraints.push_back(bit_range);
    const auto source_expression = conditional_compiled.find_expression(value);
    assert(source_expression && source_expression->vhdl != nullptr);
    auto index_value = *source_expression->vhdl;
    index_value.id = conditional_compiled.semantics.add_expression_identity(
        index_value.scope, index_value.source, index_value.origin);
    index_value.kind = semantic::vhdl::ExpressionKind::integer_literal;
    index_value.text = "0";
    index_value.referenced_name.reset();
    conditional_compiled.vhdl_hir.mutable_expressions().push_back(index_value);
    auto indexed = *source_expression->vhdl;
    indexed.id = conditional_compiled.semantics.add_expression_identity(
        indexed.scope, indexed.source, indexed.origin);
    indexed.kind = semantic::vhdl::ExpressionKind::index;
    indexed.text = "index";
    indexed.referenced_name.reset();
    indexed.operands = { value, index_value.id };
    conditional_compiled.vhdl_hir.mutable_expressions().push_back(indexed);
    auto condition = *source_expression->vhdl;
    condition.id = conditional_compiled.semantics.add_expression_identity(
        condition.scope, condition.source, condition.origin);
    condition.kind = semantic::vhdl::ExpressionKind::binary;
    condition.text = "=";
    condition.referenced_name.reset();
    condition.operands = { value, target };
    conditional_compiled.vhdl_hir.mutable_expressions().push_back(condition);
    auto conditional = *source_expression->vhdl;
    conditional.id = conditional_compiled.semantics.add_expression_identity(
        conditional.scope, conditional.source, conditional.origin);
    conditional.kind = semantic::vhdl::ExpressionKind::conditional;
    conditional.text = "when-else";
    conditional.referenced_name.reset();
    conditional.operands = { condition.id, indexed.id, target };
    conditional_compiled.vhdl_hir.mutable_expressions().push_back(conditional);
    conditional_compiled.vhdl_hir.mutable_statements().front().value
        = conditional.id;
    assert(conditional_compiled.valid());
    auto conditional_result = elaborate_compiled(
        conditional_compiled,
        "vhdl:work.hir_direct_vhdl(rtl)",
        "hir_direct_vhdl");
    assert(conditional_result.ok() && conditional_result.design);
    const auto& conditional_operations =
        conditional_result.design->processes().front().operations;
    assert(std::ranges::any_of(
        conditional_operations,
        [](const runtime::simir::Operation& operation) {
            return runtime::simir::operation_get_if<
                       runtime::simir::Extract>(&operation)
                != nullptr;
        }));
    assert(std::ranges::any_of(
        conditional_operations,
        [](const runtime::simir::Operation& operation) {
            return runtime::simir::operation_get_if<
                       runtime::simir::Branch>(&operation)
                != nullptr;
        }));

    auto dynamic_compiled = compiled;
    const auto add_scalar_range = [&](const semantic::DeclarationId id) {
        const auto found = std::ranges::find(
            dynamic_compiled.vhdl_hir.mutable_declarations(),
            id,
            &semantic::vhdl::Declaration::id);
        assert(found
            != dynamic_compiled.vhdl_hir.mutable_declarations().end());
        assert(found->subtype);
        found->subtype->type_mark.spelling = "bit_vector";
        semantic::vhdl::RangeConstraint range;
        range.left = 0;
        range.right = 0;
        range.descending = true;
        range.source = found->source;
        found->subtype->constraints.push_back(range);
    };
    add_scalar_range(a);
    add_scalar_range(q);
    const auto dynamic_process = dynamic_compiled.find_process(process);
    assert(dynamic_process && dynamic_process->vhdl != nullptr);
    const auto index_literal =
        dynamic_compiled.semantics.add_expression_identity(
            process_scope, statement_source, architecture_origin);
    semantic::vhdl::Expression index_literal_record;
    index_literal_record.id = index_literal;
    index_literal_record.scope = process_scope;
    index_literal_record.kind
        = semantic::vhdl::ExpressionKind::integer_literal;
    index_literal_record.text = "0";
    index_literal_record.source = statement_source;
    index_literal_record.origin = architecture_origin;
    dynamic_compiled.vhdl_hir.mutable_expressions().push_back(
        std::move(index_literal_record));
    const auto index_declaration =
        dynamic_compiled.semantics.add_declaration(
            process_scope,
            semantic::DeclarationKind::variable,
            "selected_index",
            process_source,
            architecture_origin);
    semantic::vhdl::Declaration index_declaration_record;
    index_declaration_record.id = index_declaration;
    index_declaration_record.scope = process_scope;
    index_declaration_record.form
        = semantic::vhdl::DeclarationForm::variable;
    index_declaration_record.name = "selected_index";
    index_declaration_record.source = process_source;
    index_declaration_record.origin = architecture_origin;
    index_declaration_record.object_class
        = semantic::vhdl::ObjectClass::variable;
    index_declaration_record.initializer = index_literal;
    index_declaration_record.subtype.emplace();
    index_declaration_record.subtype->type_mark.source = process_source;
    index_declaration_record.subtype->type_mark.spelling = "integer";
    index_declaration_record.subtype->domain
        = semantic::vhdl::ValueDomain::integer;
    index_declaration_record.subtype->signed_value = true;
    index_declaration_record.subtype->executable_width = 32U;
    dynamic_compiled.vhdl_hir.mutable_declarations().push_back(
        std::move(index_declaration_record));
    dynamic_compiled.vhdl_hir.mutable_processes().front().declarations
        .push_back(index_declaration);
    const auto index_name =
        dynamic_compiled.semantics.add_expression_identity(
            process_scope, statement_source, architecture_origin);
    semantic::vhdl::Expression index_name_record;
    index_name_record.id = index_name;
    index_name_record.scope = process_scope;
    index_name_record.kind = semantic::vhdl::ExpressionKind::name;
    index_name_record.text = "selected_index";
    index_name_record.source = statement_source;
    index_name_record.origin = architecture_origin;
    index_name_record.referenced_name.emplace();
    index_name_record.referenced_name->spelling = "selected_index";
    index_name_record.referenced_name->canonical = "selected_index";
    index_name_record.referenced_name->source = statement_source;
    index_name_record.referenced_name->selected = index_declaration;
    dynamic_compiled.vhdl_hir.mutable_expressions().push_back(
        std::move(index_name_record));
    const auto add_dynamic_index = [&](const semantic::ExpressionId object) {
        const auto dynamic_source_expression = dynamic_compiled.find_expression(
            object);
        assert(dynamic_source_expression
            && dynamic_source_expression->vhdl != nullptr);
        auto dynamic_indexed = *dynamic_source_expression->vhdl;
        dynamic_indexed.id = dynamic_compiled.semantics.add_expression_identity(
            process_scope, statement_source, architecture_origin);
        dynamic_indexed.kind = semantic::vhdl::ExpressionKind::index;
        dynamic_indexed.text = "index";
        dynamic_indexed.referenced_name.reset();
        dynamic_indexed.operands = { object, index_name };
        dynamic_compiled.vhdl_hir.mutable_expressions().push_back(
            dynamic_indexed);
        return dynamic_indexed.id;
    };
    const auto dynamic_target = add_dynamic_index(target);
    const auto dynamic_value = add_dynamic_index(value);
    dynamic_compiled.vhdl_hir.mutable_statements().front().target
        = dynamic_target;
    dynamic_compiled.vhdl_hir.mutable_statements().front().value
        = dynamic_value;
    assert(dynamic_compiled.valid());
    auto dynamic_result = elaborate_compiled(
        dynamic_compiled,
        "vhdl:work.hir_direct_vhdl(rtl)",
        "hir_direct_vhdl");
    assert(dynamic_result.ok() && dynamic_result.design);
    const auto& dynamic_operations =
        dynamic_result.design->processes().front().operations;
    assert(std::ranges::any_of(
        dynamic_operations,
        [](const runtime::simir::Operation& operation) {
            return runtime::simir::operation_get_if<
                       runtime::simir::DynamicExtract>(&operation)
                != nullptr;
        }));
    assert(std::ranges::any_of(
        dynamic_operations,
        [](const runtime::simir::Operation& operation) {
            return runtime::simir::operation_get_if<
                       runtime::simir::WriteUpdateDynamicSlice>(&operation)
                    != nullptr
                || runtime::simir::operation_get_if<
                       runtime::simir::WriteProjectedDynamicSlice>(
                       &operation)
                    != nullptr;
        }));
    const auto dynamic_a = dynamic_result.design->find_signal("a");
    const auto dynamic_q = dynamic_result.design->find_signal("q");
    assert(dynamic_a && dynamic_q);
    auto dynamic_interpreter = dynamic_result.design->create_interpreter();
    dynamic_interpreter->deposit_signal(
        *dynamic_a, runtime::PackedLogic4::from_msb_string("1"));
    dynamic_interpreter->start();
    (void)dynamic_interpreter->run();
    assert(dynamic_interpreter->signal_value(*dynamic_q).to_msb_string()
        == "1");
}

void run_linked_child_occurrence_test()
{
    const auto parse_unit = [](const std::string& source,
                                const std::string_view text,
                                const std::string_view library) {
        auto parsed = frontend::parse_text(
            source, text, frontend::Language::SystemVerilog2017);
        assert(parsed.ok() && parsed.design.units.size() == 1U);
        auto unit = std::move(parsed.design.units.front());
        unit.library = library;
        return unit;
    };
    auto top_adapter = parse_unit(
        "linked-top.sv",
        "module linked_top; leaf child(); endmodule",
        "work");
    auto work_leaf_adapter = parse_unit(
        "linked-work-leaf.sv",
        "module leaf; endmodule",
        "work");
    auto alternate_leaf_adapter = parse_unit(
        "linked-alt-leaf.sv",
        "module leaf; endmodule",
        "alternate");
    assert(top_adapter.instances.size() == 1U);

    const auto make_bundle = [](
                                 const frontend::DesignUnit& adapter,
                                 const bool owns_instance) {
        semantic::Model model;
        semantic::sv::Hir systemverilog;
        semantic::vhdl::Hir vhdl;
        const auto source_name = std::string {
            frontend::physical_source(adapter.span) };
        const auto file = model.intern_source_file(
            source_name, source_name + "-digest");
        const auto unit_source = add_source_span(
            model, file, adapter.span);
        const auto origin = model.add_origin(
            semantic::OriginKind::parsed, unit_source);
        const auto unit = model.add_unit(
            semantic::Language::system_verilog,
            semantic::UnitKind::verilog_module,
            adapter.library,
            adapter.name,
            { },
            unit_source,
            origin);
        const auto scope = model.units()[unit.value()].scope;

        semantic::sv::Unit unit_record;
        unit_record.id = unit;
        unit_record.scope = scope;
        unit_record.kind = semantic::sv::UnitKind::module;
        unit_record.library = adapter.library;
        unit_record.name = adapter.name;
        unit_record.source = unit_source;
        unit_record.origin = origin;
        if (owns_instance) {
            assert(adapter.instances.size() == 1U);
            const auto& source_instance = adapter.instances.front();
            const auto instance_source = add_source_span(
                model, file, source_instance.span);
            const auto instance = model.add_instance(
                scope,
                source_instance.name,
                "alternate.leaf",
                instance_source,
                origin);
            semantic::sv::Instance instance_record;
            instance_record.id = instance;
            instance_record.scope = scope;
            instance_record.target.spelling = "alternate.leaf";
            instance_record.target.source = instance_source;
            instance_record.name = source_instance.name;
            instance_record.source = instance_source;
            instance_record.origin = origin;
            systemverilog.mutable_instances().push_back(
                std::move(instance_record));
            unit_record.instances.push_back(instance);
        }
        systemverilog.mutable_units().push_back(
            std::move(unit_record));
        semantic::CompiledDesign result {
            std::move(model),
            std::move(systemverilog),
            std::move(vhdl),
        };
        assert(result.valid());
        return result;
    };

    auto linked = semantic::link_compiled_designs({
        make_bundle(work_leaf_adapter, false),
        make_bundle(top_adapter, true),
        make_bundle(alternate_leaf_adapter, false),
    });
    assert(linked.ok());
    auto compiled = std::move(*linked.design);
    const auto top = compiled.find_unit(
        semantic::UnitKind::verilog_module,
        "work",
        "linked_top");
    const auto alternate_leaf = compiled.find_unit(
        semantic::UnitKind::verilog_module,
        "alternate",
        "leaf");
    assert(top && top->systemverilog != nullptr);
    assert(alternate_leaf && alternate_leaf->systemverilog != nullptr);
    assert(top->systemverilog->instances.size() == 1U);
    const auto source_instance = top->systemverilog->instances.front();
    assert(std::ranges::any_of(
        compiled.references(),
        [&](const semantic::CompiledReference& reference) {
            return reference.owner == top->identity->id
                && reference.source
                    == compiled.find_instance(source_instance)
                           ->systemverilog->source
                && reference.target == alternate_leaf->identity->id;
        }));

    const auto result = elaborate_compiled(
        compiled,
        "sv:work.linked_top",
        "linked_top");
    if (!result.ok()) {
        for (const auto& diagnostic : result.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(result.ok() && result.design);
    const auto specialization = [&](const std::string_view path)
        -> const fsim::elaboration::SpecializationInfo* {
        const auto found = std::ranges::find(
            result.design->specializations(), path,
            &fsim::elaboration::SpecializationInfo::instance);
        return found == result.design->specializations().end()
            ? nullptr
            : &*found;
    };
    const auto* root = specialization("linked_top");
    const auto* child = specialization("linked_top.child");
    assert(root != nullptr && child != nullptr);
    assert(child->library == "alternate");
    assert(root->source_unit == top->identity->id);
    assert(root->source_span == top->identity->source);
    assert(root->origin == top->identity->origin);
    assert(child->source_unit == alternate_leaf->identity->id);
    assert(child->source_instance == source_instance);
    assert(child->source_span == alternate_leaf->identity->source);
    assert(child->origin == alternate_leaf->identity->origin);
}

} // namespace

void test_direct_hir_lowering()
{
    run_systemverilog_direct_hir_test();
    run_systemverilog_unsized_arithmetic_test();
    run_systemverilog_procedural_update_test();
    run_systemverilog_operator_and_nested_update_test();
    run_systemverilog_formatted_string_hir_test();
    run_systemverilog_system_function_assignment_test();
    run_systemverilog_generate_identity_assignment_test();
    run_systemverilog_hir_recursive_function_test();
    run_unavailable_hir_boundary_test();
    run_residual_overlay_boundary_test();
    run_conservative_expression_boundary_tests();
    run_composite_expression_test();
    run_dynamic_packed_selection_test();
    run_statically_bounded_loop_test();
    run_local_nonblocking_boundary_test();
    run_systemverilog_equality_test();
    run_vhdl_direct_hir_test();
    run_linked_child_occurrence_test();
}

} // namespace fsim::tests::elaboration
