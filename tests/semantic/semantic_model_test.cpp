// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/model.hpp"

#include <array>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

void run_compiled_design_tests();

int main() {
    using namespace fsim::semantic;

    Model model;
    const auto source = model.intern_source_file(
        "/physical/source.sv", "0123456789abcdef");
    assert(source.valid() && source.value() == 0);
    assert(
        model.intern_source_file(
            "/physical/source.sv", "0123456789abcdef")
        == source);

    const std::array<std::string, 2> expansion_descriptions{
        "expanded macro OUTER at source.sv:2:1",
        "expanded macro INNER at source.sv:3:5"};
    const auto expansion = model.intern_expansion(expansion_descriptions);
    assert(expansion.valid() && expansion.value() == 1);
    assert(model.expansions().size() == 2);
    assert(model.expansions()[1].parent == model.expansions()[0].id);
    assert(model.intern_expansion(expansion_descriptions) == expansion);

    const auto span = model.intern_source_span(
        source,
        "logical_name.sv",
        {17, 3, 5},
        {23, 3, 11},
        expansion);
    assert(span.valid() && span.value() == 0);
    assert(
        model.intern_source_span(
            source,
            "logical_name.sv",
            {17, 3, 5},
            {23, 3, 11},
            expansion)
        == span);

    const auto parsed = model.add_origin(
        OriginKind::parsed, span, std::nullopt, "module counter");
    const auto specialized = model.add_origin(
        OriginKind::specialized, span, parsed, "counter WIDTH=8");
    const auto unit = model.add_unit(
        Language::system_verilog,
        UnitKind::verilog_module,
        "work",
        "counter",
        {},
        span,
        parsed);
    assert(unit.valid() && unit.value() == 0);
    assert(model.units().front().scope.value() == 0);

    const auto packed = model.add_type(
        model.units().front().scope,
        TypeKind::declaration,
        "word_t",
        {{}, span, "logic [7:0]"},
        span,
        parsed);
    const auto value = model.add_value(
        model.units().front().scope,
        ValueKind::signal,
        "data",
        {packed, span, "word_t"},
        span,
        specialized);
    const auto instance = model.add_instance(
        model.units().front().scope,
        "child",
        "work.counter",
        span,
        specialized);
    const auto declaration = model.add_declaration(
        model.units().front().scope,
        DeclarationKind::signal,
        "data",
        span,
        parsed);
    const auto expression = model.add_expression_identity(
        model.units().front().scope, span, parsed);
    const auto statement = model.add_statement_identity(
        model.units().front().scope, span, parsed);
    const auto process = model.add_process_identity(
        model.units().front().scope, "drive", span, parsed);
    assert(packed.value() == 0 && value.value() == 0);
    assert(instance.value() == 0);
    assert(declaration.value() == 0);
    assert(expression.value() == 0);
    assert(statement.value() == 0);
    assert(process.value() == 0);
    assert(model.declarations().front().name == "data");
    assert(model.expression_identities().front().source == span);
    assert(model.statement_identities().front().scope
           == model.units().front().scope);
    assert(model.process_identities().front().name == "drive");
    assert(model.values().front().type.resolved());
    assert(model.values().front().type.target == packed);
    assert(model.valid());

    Model invalid_model;
    const auto invalid_file = invalid_model.intern_source_file("invalid.sv");
    const auto invalid_span = invalid_model.intern_source_span(
        invalid_file, {}, {0, 1, 1}, {1, 1, 2});
    const auto invalid_origin = invalid_model.add_origin(
        OriginKind::parsed, invalid_span);
    const auto invalid_unit = invalid_model.add_unit(
        Language::system_verilog,
        UnitKind::verilog_module,
        "work",
        "invalid",
        {},
        invalid_span,
        invalid_origin);
    (void)invalid_model.add_type(
        invalid_model.units()[invalid_unit.value()].scope,
        TypeKind::alias,
        "missing_t",
        {TypeId::from_index(7), invalid_span, "missing_t"},
        invalid_span,
        invalid_origin);
    assert(!invalid_model.valid());

    bool rejected = false;
    try {
        (void)model.intern_source_span(
            source, "logical_name.sv", {8, 2, 8}, {7, 2, 7});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);

    auto records = std::move(model).take_records();
    assert(model.valid());
    assert(model.source_files().empty());
    assert(model.expansions().empty());
    assert(model.source_spans().empty());
    assert(model.origins().empty());
    assert(model.scopes().empty());
    assert(model.units().empty());
    assert(model.types().empty());
    assert(model.values().empty());
    assert(model.instances().empty());
    assert(model.declarations().empty());
    assert(model.expression_identities().empty());
    assert(model.statement_identities().empty());
    assert(model.process_identities().empty());
    assert(model.intern_source_file("reused.sv").value() == 0);

    auto rebuilt = Model::from_records(std::move(records));
    assert(rebuilt && rebuilt->valid());
    assert(rebuilt->source_files().size() == 1);
    assert(rebuilt->expansions().size() == 2);
    assert(rebuilt->source_spans().size() == 1);
    assert(rebuilt->origins().size() == 2);
    assert(rebuilt->units().size() == 1);
    assert(rebuilt->types().size() == 1);
    assert(rebuilt->values().size() == 1);
    assert(rebuilt->instances().size() == 1);
    assert(rebuilt->declarations().size() == 1);
    assert(rebuilt->expression_identities().size() == 1);
    assert(rebuilt->statement_identities().size() == 1);
    assert(rebuilt->process_identities().size() == 1);

    run_compiled_design_tests();
    std::cout << "semantic identity and provenance tests passed\n";
}
