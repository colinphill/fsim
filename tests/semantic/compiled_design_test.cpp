// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/compiled_design.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"
#include "fsim/semantic/compiled_design_normalization.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"
#include "fsim/semantic/compiled_design_specialization.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <ranges>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using namespace fsim::semantic;

struct LinkBundleBuilder {
    Model model;
    sv::Hir systemverilog;
    vhdl::Hir vhdl_hir;
    SourceSpanId source;
    OriginId origin;

    explicit LinkBundleBuilder(const std::string& logical_name)
    {
        const auto file = model.intern_source_file(
            "/checkout/" + logical_name, logical_name + "-digest");
        source = model.intern_source_span(
            file, logical_name, { 0, 1, 1 }, { 10, 1, 11 });
        origin = model.add_origin(OriginKind::parsed, source);
    }

    UnitId add_systemverilog_unit(const UnitKind kind,
        const sv::UnitKind hir_kind, std::string name,
        std::string library = "work",
        const Language language = Language::system_verilog)
    {
        assert(language == Language::verilog
            || language == Language::system_verilog);
        const auto id = model.add_unit(language, kind,
            library, name, {}, source, origin);
        const auto& identity = model.units()[id.value()];
        sv::Unit unit;
        unit.id = id;
        unit.scope = identity.scope;
        unit.kind = hir_kind;
        unit.library = std::move(library);
        unit.name = std::move(name);
        unit.source = source;
        unit.origin = origin;
        systemverilog.mutable_units().push_back(std::move(unit));
        return id;
    }

    UnitId add_vhdl_unit(const UnitKind kind,
        const vhdl::UnitKind hir_kind, std::string name,
        std::string primary_name = {}, std::string library = "work")
    {
        const auto id = model.add_unit(Language::vhdl, kind,
            library, name, primary_name, source, origin);
        const auto& identity = model.units()[id.value()];
        vhdl::Unit unit;
        unit.id = id;
        unit.scope = identity.scope;
        unit.kind = hir_kind;
        unit.library = std::move(library);
        unit.name = std::move(name);
        unit.primary_name = std::move(primary_name);
        unit.source = source;
        unit.origin = origin;
        vhdl_hir.mutable_units().push_back(std::move(unit));
        return id;
    }

    sv::Unit& systemverilog_unit(const UnitId id)
    {
        const auto found = std::ranges::find(
            systemverilog.mutable_units(), id, &sv::Unit::id);
        assert(found != systemverilog.mutable_units().end());
        return *found;
    }

    vhdl::Unit& vhdl_unit(const UnitId id)
    {
        const auto found = std::ranges::find(
            vhdl_hir.mutable_units(), id, &vhdl::Unit::id);
        assert(found != vhdl_hir.mutable_units().end());
        return *found;
    }

    void add_systemverilog_graph(const UnitId owner,
        const std::string& instance_target, const std::string& literal)
    {
        const auto scope = model.units()[owner.value()].scope;
        const auto declaration = model.add_declaration(scope,
            DeclarationKind::variable, "payload", source, origin);
        TypeReference base;
        base.source = source;
        base.spelling = "int";
        const auto type = model.add_type(scope, TypeKind::declaration,
            "payload_t", base, source, origin);
        sv::TypeDefinition type_record;
        type_record.id = type;
        type_record.declaration = declaration;
        type_record.name = "payload_t";
        type_record.base.target.source = source;
        type_record.base.target.spelling = "int";
        type_record.source = source;
        type_record.origin = origin;
        systemverilog.mutable_types().push_back(std::move(type_record));

        sv::Declaration declaration_record;
        declaration_record.id = declaration;
        declaration_record.scope = scope;
        declaration_record.name = "payload";
        declaration_record.source = source;
        declaration_record.origin = origin;
        declaration_record.declared_type = type;
        systemverilog.mutable_declarations().push_back(
            std::move(declaration_record));

        const auto expression = model.add_expression_identity(
            scope, source, origin);
        sv::Expression expression_record;
        expression_record.id = expression;
        expression_record.scope = scope;
        expression_record.kind = sv::ExpressionKind::integer_literal;
        expression_record.text = literal;
        expression_record.source = source;
        expression_record.origin = origin;
        systemverilog.mutable_expressions().push_back(
            std::move(expression_record));

        const auto statement = model.add_statement_identity(
            scope, source, origin);
        sv::Statement statement_record;
        statement_record.id = statement;
        statement_record.scope = scope;
        statement_record.kind = sv::StatementKind::assignment;
        statement_record.value = expression;
        statement_record.source = source;
        statement_record.origin = origin;
        systemverilog.mutable_statements().push_back(
            std::move(statement_record));

        const auto process = model.add_process_identity(
            scope, "main", source, origin);
        sv::Process process_record;
        process_record.id = process;
        process_record.scope = scope;
        process_record.name = "main";
        process_record.source = source;
        process_record.origin = origin;
        process_record.declarations.push_back(declaration);
        process_record.statements.push_back(statement);
        systemverilog.mutable_processes().push_back(
            std::move(process_record));

        const auto instance = model.add_instance(scope, "child",
            instance_target, source, origin);
        sv::Instance instance_record;
        instance_record.id = instance;
        instance_record.scope = scope;
        instance_record.target.spelling = instance_target;
        instance_record.target.source = source;
        instance_record.name = "child";
        instance_record.source = source;
        instance_record.origin = origin;
        sv::ActualAssociation parameter;
        parameter.expression = expression;
        parameter.source = source;
        instance_record.parameters.push_back(std::move(parameter));
        systemverilog.mutable_instances().push_back(
            std::move(instance_record));

        auto& unit = systemverilog_unit(owner);
        unit.declarations.push_back(declaration);
        unit.processes.push_back(process);
        unit.instances.push_back(instance);
    }

    void add_vhdl_graph(const UnitId owner,
        const std::string& instance_target, const std::string& literal)
    {
        const auto scope = model.units()[owner.value()].scope;
        const auto declaration = model.add_declaration(scope,
            DeclarationKind::variable, "payload", source, origin);
        TypeReference base;
        base.source = source;
        base.spelling = "integer";
        const auto type = model.add_type(scope, TypeKind::declaration,
            "payload_t", base, source, origin);
        vhdl::TypeDefinition type_record;
        type_record.id = type;
        type_record.declaration = declaration;
        type_record.name = "payload_t";
        type_record.base.type_mark.source = source;
        type_record.base.type_mark.spelling = "integer";
        type_record.source = source;
        type_record.origin = origin;
        vhdl_hir.mutable_types().push_back(std::move(type_record));

        vhdl::Declaration declaration_record;
        declaration_record.id = declaration;
        declaration_record.scope = scope;
        declaration_record.name = "payload";
        declaration_record.source = source;
        declaration_record.origin = origin;
        declaration_record.declared_type = type;
        vhdl_hir.mutable_declarations().push_back(
            std::move(declaration_record));

        const auto expression = model.add_expression_identity(
            scope, source, origin);
        vhdl::Expression expression_record;
        expression_record.id = expression;
        expression_record.scope = scope;
        expression_record.kind = vhdl::ExpressionKind::integer_literal;
        expression_record.text = literal;
        expression_record.source = source;
        expression_record.origin = origin;
        vhdl_hir.mutable_expressions().push_back(
            std::move(expression_record));

        const auto statement = model.add_statement_identity(
            scope, source, origin);
        vhdl::Statement statement_record;
        statement_record.id = statement;
        statement_record.scope = scope;
        statement_record.kind = vhdl::StatementKind::variable_assignment;
        statement_record.value = expression;
        statement_record.source = source;
        statement_record.origin = origin;
        vhdl_hir.mutable_statements().push_back(
            std::move(statement_record));

        const auto process = model.add_process_identity(
            scope, "main", source, origin);
        vhdl::Process process_record;
        process_record.id = process;
        process_record.scope = scope;
        process_record.name = "main";
        process_record.source = source;
        process_record.origin = origin;
        process_record.declarations.push_back(declaration);
        process_record.statements.push_back(statement);
        vhdl_hir.mutable_processes().push_back(
            std::move(process_record));

        const auto instance = model.add_instance(scope, "child",
            instance_target, source, origin);
        vhdl::Instance instance_record;
        instance_record.id = instance;
        instance_record.scope = scope;
        instance_record.target.spelling = instance_target;
        instance_record.target.canonical = instance_target;
        instance_record.target.source = source;
        instance_record.name = "child";
        instance_record.source = source;
        instance_record.origin = origin;
        vhdl::Association generic;
        generic.expression = expression;
        generic.source = source;
        instance_record.generic_map.push_back(std::move(generic));
        vhdl_hir.mutable_instances().push_back(
            std::move(instance_record));

        auto& unit = vhdl_unit(owner);
        unit.declarations.push_back(declaration);
        unit.processes.push_back(process);
        unit.instances.push_back(instance);
    }

    CompiledDesign finish()
    {
        return CompiledDesign { std::move(model),
            std::move(systemverilog), std::move(vhdl_hir) };
    }
};

CompiledDesign make_compiled_design()
{
    Model model;
    sv::Hir systemverilog;
    vhdl::Hir vhdl_hir;
    const auto file = model.intern_source_file(
        "compiled-design-test", "compiled-design-digest");
    const auto source = model.intern_source_span(
        file, "compiled-design-test", { 0, 1, 1 }, { 1, 1, 2 });
    const auto origin = model.add_origin(OriginKind::parsed, source);

    const auto add_systemverilog = [&](const Language language,
                                       const UnitKind kind,
                                       const sv::UnitKind hir_kind,
                                       std::string library,
                                       std::string name) {
        const auto id = model.add_unit(language, kind,
            std::move(library), std::move(name), {}, source, origin);
        const auto& identity = model.units()[id.value()];
        sv::Unit unit;
        unit.id = id;
        unit.scope = identity.scope;
        unit.kind = hir_kind;
        unit.library = identity.library;
        unit.name = identity.name;
        unit.source = source;
        unit.origin = origin;
        systemverilog.mutable_units().push_back(std::move(unit));
    };
    const auto add_vhdl = [&](const UnitKind kind,
                              const vhdl::UnitKind hir_kind,
                              std::string library,
                              std::string name,
                              std::string primary_name = {}) {
        const auto id = model.add_unit(Language::vhdl, kind,
            std::move(library), std::move(name),
            std::move(primary_name), source, origin);
        const auto& identity = model.units()[id.value()];
        vhdl::Unit unit;
        unit.id = id;
        unit.scope = identity.scope;
        unit.kind = hir_kind;
        unit.library = identity.library;
        unit.name = identity.name;
        unit.primary_name = identity.secondary_name;
        unit.source = source;
        unit.origin = origin;
        vhdl_hir.mutable_units().push_back(std::move(unit));
    };

    add_systemverilog(Language::verilog, UnitKind::verilog_module,
        sv::UnitKind::module, {}, "module_unit");
    add_systemverilog(Language::system_verilog,
        UnitKind::systemverilog_interface, sv::UnitKind::interface,
        "work", "shared_unit");
    add_systemverilog(Language::system_verilog,
        UnitKind::systemverilog_program, sv::UnitKind::program,
        {}, "program_unit");
    add_systemverilog(Language::system_verilog,
        UnitKind::systemverilog_configuration,
        sv::UnitKind::configuration, {}, "configuration_unit");
    add_systemverilog(Language::system_verilog,
        UnitKind::systemverilog_package, sv::UnitKind::package,
        {}, "shared_unit");
    add_systemverilog(Language::system_verilog,
        UnitKind::systemverilog_bind, sv::UnitKind::bind,
        {}, "bind_unit");

    add_vhdl(UnitKind::vhdl_entity, vhdl::UnitKind::entity,
        {}, "entity_unit");
    add_vhdl(UnitKind::vhdl_architecture, vhdl::UnitKind::architecture,
        {}, "rtl", "entity_unit");
    add_vhdl(UnitKind::vhdl_configuration, vhdl::UnitKind::configuration,
        {}, "vhdl_configuration", "entity_unit");
    add_vhdl(UnitKind::vhdl_package, vhdl::UnitKind::package,
        {}, "vhdl_package");
    add_vhdl(UnitKind::vhdl_context, vhdl::UnitKind::context,
        {}, "vhdl_context");
    add_vhdl(UnitKind::vhdl_psl_verification_unit,
        vhdl::UnitKind::psl_verification_unit,
        {}, "vhdl_psl");

    return CompiledDesign {
        std::move(model), std::move(systemverilog), std::move(vhdl_hir) };
}

void assert_systemverilog_lookup(const CompiledDesign& design,
    const UnitKind kind, const Language language,
    const std::string_view name)
{
    const auto by_kind = design.find_unit(kind, "work", name);
    assert(by_kind && *by_kind);
    assert(by_kind->identity != nullptr);
    assert(by_kind->identity->kind == kind);
    assert(by_kind->language == language);
    assert(by_kind->systemverilog != nullptr);
    assert(by_kind->vhdl == nullptr);
    const auto by_id = design.find_unit(by_kind->identity->id);
    assert(by_id);
    assert(by_id->identity == by_kind->identity);
}

void assert_vhdl_lookup(const CompiledDesign& design,
    const UnitKind kind, const std::string_view name,
    const std::string_view secondary_name = {})
{
    const auto by_kind = design.find_unit(
        kind, {}, name, secondary_name);
    assert(by_kind && *by_kind);
    assert(by_kind->identity != nullptr);
    assert(by_kind->identity->kind == kind);
    assert(by_kind->language == Language::vhdl);
    assert(by_kind->systemverilog == nullptr);
    assert(by_kind->vhdl != nullptr);
    const auto by_language = design.find_unit(
        Language::vhdl, "work", name, secondary_name);
    assert(by_language);
    assert(by_language->identity == by_kind->identity);
    const auto by_id = design.find_unit(by_kind->identity->id);
    assert(by_id);
    assert(by_id->identity == by_kind->identity);
}

sv::Name sv_name(std::string spelling, const SourceSpanId source)
{
    sv::Name result;
    result.spelling = std::move(spelling);
    result.source = source;
    return result;
}

vhdl::Name vhdl_name(std::string spelling, const SourceSpanId source)
{
    vhdl::Name result;
    result.spelling = spelling;
    result.canonical = std::move(spelling);
    result.source = source;
    return result;
}

sv::UdpDeclaration valid_udp_declaration(
    std::string name, const SourceSpanId source, const OriginId origin)
{
    sv::UdpDeclaration declaration;
    declaration.language = Language::system_verilog;
    declaration.standard = "systemverilog-2017";
    declaration.compatibility_profile = "none";
    declaration.library = "work";
    declaration.name = std::move(name);
    declaration.output = "q";
    declaration.inputs.push_back("d");
    sv::UdpTableRow row;
    row.inputs.push_back({ sv::UdpLevel::dont_care,
        sv::UdpEdge::none, sv::UdpLevel::unknown,
        sv::UdpLevel::unknown, source });
    row.output = sv::UdpOutput::unknown;
    row.source = source;
    declaration.rows.push_back(std::move(row));
    declaration.source = source;
    declaration.origin = origin;
    return declaration;
}

CompiledDesign make_link_provider()
{
    LinkBundleBuilder builder { "provider.sv" };
    const auto package = builder.add_systemverilog_unit(
        UnitKind::systemverilog_package, sv::UnitKind::package, "Leaf");
    const auto leaf = builder.add_systemverilog_unit(
        UnitKind::verilog_module, sv::UnitKind::module,
        "Leaf", "work", Language::verilog);
    builder.add_systemverilog_unit(UnitKind::systemverilog_interface,
        sv::UnitKind::interface, "Iface");
    builder.add_systemverilog_unit(UnitKind::systemverilog_program,
        sv::UnitKind::program, "ProgramUnit");
    const auto configuration = builder.add_systemverilog_unit(
        UnitKind::systemverilog_configuration,
        sv::UnitKind::configuration, "SvConfig");
    builder.add_systemverilog_unit(UnitKind::systemverilog_bind,
        sv::UnitKind::bind, "BindUnit");
    builder.add_systemverilog_graph(leaf, "Iface", "1");

    sv::ConfigurationDeclaration configuration_declaration;
    sv::ConfigurationDesign design;
    design.library = "work";
    design.cell = "Leaf";
    design.source = builder.source;
    design.origin = builder.origin;
    design.target = leaf;
    configuration_declaration.designs.push_back(std::move(design));
    sv::ConfigurationRule rule;
    rule.kind = sv::ConfigurationRuleKind::cell;
    rule.selection = sv::ConfigurationSelectionKind::use;
    rule.selector = "Leaf";
    rule.use_library = "work";
    rule.use_cell = "Leaf";
    rule.source = builder.source;
    rule.origin = builder.origin;
    rule.target = leaf;
    configuration_declaration.rules.push_back(std::move(rule));
    configuration_declaration.source = builder.source;
    configuration_declaration.origin = builder.origin;
    builder.systemverilog_unit(configuration).configuration
        = std::move(configuration_declaration);

    sv::ClassDeclaration base_class;
    base_class.name = "Base";
    base_class.canonical_identity = "work::Leaf::Base";
    base_class.enclosing_identity = "work::Leaf";
    base_class.source = builder.source;
    base_class.origin = builder.model.add_origin(OriginKind::parsed,
        builder.source, builder.model.units()[package.value()].origin,
        base_class.canonical_identity);
    base_class.scope = builder.model.add_scope(package,
        builder.model.units()[package.value()].scope, base_class.name,
        base_class.source, base_class.origin);
    builder.systemverilog.mutable_classes().push_back(
        std::move(base_class));
    sv::ClassDeclaration deep_class;
    deep_class.name = "DeepOnly";
    deep_class.canonical_identity = "work::Leaf::DeepOnly";
    deep_class.enclosing_identity = "work::Leaf";
    deep_class.source = builder.source;
    deep_class.origin = builder.model.add_origin(OriginKind::parsed,
        builder.source, builder.model.units()[package.value()].origin,
        deep_class.canonical_identity);
    deep_class.scope = builder.model.add_scope(package,
        builder.model.units()[package.value()].scope, deep_class.name,
        deep_class.source, deep_class.origin);
    builder.systemverilog.mutable_classes().push_back(
        std::move(deep_class));
    sv::ClassDeclaration block_class;
    block_class.name = "BlockOnly";
    block_class.canonical_identity = "work::Leaf::BlockOnly";
    block_class.enclosing_identity = "work::Leaf";
    block_class.source = builder.source;
    block_class.origin = builder.model.add_origin(OriginKind::parsed,
        builder.source, builder.model.units()[package.value()].origin,
        block_class.canonical_identity);
    block_class.scope = builder.model.add_scope(package,
        builder.model.units()[package.value()].scope, block_class.name,
        block_class.source, block_class.origin);
    builder.systemverilog.mutable_classes().push_back(
        std::move(block_class));

    builder.systemverilog.mutable_udps().push_back(
        valid_udp_declaration(
            "provider_udp", builder.source, builder.origin));

    builder.add_vhdl_unit(UnitKind::vhdl_package,
        vhdl::UnitKind::package, "VHDL_PKG", {}, "WORK");
    const auto shared_context = builder.add_vhdl_unit(
        UnitKind::vhdl_context, vhdl::UnitKind::context,
        "SharedContext", {}, "WORK");
    vhdl::ContextItem shared_use_clause;
    shared_use_clause.kind = vhdl::ContextKind::use_clause;
    shared_use_clause.source = builder.source;
    shared_use_clause.selected_names.push_back(
        vhdl_name("work.VHDL_PKG.all", builder.source));
    builder.vhdl_unit(shared_context).context.push_back(
        std::move(shared_use_clause));
    builder.add_vhdl_unit(UnitKind::vhdl_entity,
        vhdl::UnitKind::entity, "EntityUnit", {}, "WORK");
    builder.add_vhdl_unit(UnitKind::vhdl_architecture,
        vhdl::UnitKind::architecture, "RTL", "EntityUnit", "WORK");
    builder.add_vhdl_unit(UnitKind::vhdl_architecture,
        vhdl::UnitKind::architecture, "Gate", "EntityUnit", "WORK");
    builder.add_vhdl_unit(UnitKind::vhdl_configuration,
        vhdl::UnitKind::configuration, "VhdlCfg",
        "EntityUnit", "WORK");
    return builder.finish();
}

CompiledDesign make_link_consumer()
{
    LinkBundleBuilder builder { "consumer.vhd" };
    const auto consumer = builder.add_systemverilog_unit(
        UnitKind::verilog_module, sv::UnitKind::module, "Consumer");
    const auto consumer_package = builder.add_systemverilog_unit(
        UnitKind::systemverilog_package,
        sv::UnitKind::package, "ConsumerPkg");
    builder.add_systemverilog_graph(consumer, "Leaf", "42");
    auto& systemverilog = builder.systemverilog_unit(consumer);
    sv::Import import;
    import.package = sv_name("work::Leaf", builder.source);
    import.wildcard = true;
    import.source = builder.source;
    systemverilog.imports.push_back(std::move(import));
    sv::Export export_item;
    export_item.package = sv_name("work::Leaf", builder.source);
    export_item.wildcard = true;
    export_item.source = builder.source;
    systemverilog.exports.push_back(std::move(export_item));
    for (const auto target : { "Iface", "ProgramUnit" }) {
        const auto scope = builder.model.units()[consumer.value()].scope;
        const auto id = builder.model.add_instance(scope,
            std::string { "child_" } + target, target,
            builder.source, builder.origin);
        sv::Instance instance;
        instance.id = id;
        instance.scope = scope;
        instance.target = sv_name(target, builder.source);
        instance.name = std::string { "child_" } + target;
        instance.source = builder.source;
        instance.origin = builder.origin;
        builder.systemverilog.mutable_instances().push_back(
            std::move(instance));
        systemverilog.instances.push_back(id);
    }
    sv::BindDirective bind;
    bind.target = sv_name("Leaf", builder.source);
    bind.source = builder.source;
    bind.origin = builder.origin;
    systemverilog.binds.push_back(std::move(bind));

    const auto package_scope
        = builder.model.units()[consumer_package.value()].scope;
    const std::string derived_identity = "work::ConsumerPkg::Derived";
    const auto class_origin = builder.model.add_origin(OriginKind::parsed,
        builder.source, builder.model.units()[consumer_package.value()].origin,
        derived_identity);
    const auto class_scope = builder.model.add_scope(
        consumer_package, package_scope, "Derived",
        builder.source, class_origin);
    const auto parameter_origin = builder.model.add_origin(
        OriginKind::parsed, builder.source, class_origin, "COUNT");
    const auto property_origin = builder.model.add_origin(
        OriginKind::parsed, builder.source, class_origin, "payload");
    const auto parameter_declaration = builder.model.add_declaration(
        class_scope, DeclarationKind::generic, "COUNT",
        builder.source, parameter_origin);
    const auto property_declaration = builder.model.add_declaration(
        class_scope, DeclarationKind::variable, "payload",
        builder.source, property_origin);
    const auto class_default = builder.model.add_expression_identity(
        class_scope, builder.source, builder.origin);
    sv::Expression class_default_record;
    class_default_record.id = class_default;
    class_default_record.scope = class_scope;
    class_default_record.kind = sv::ExpressionKind::integer_literal;
    class_default_record.text = "17";
    class_default_record.source = builder.source;
    class_default_record.origin = builder.origin;
    builder.systemverilog.mutable_expressions().push_back(
        std::move(class_default_record));
    sv::Declaration parameter_record;
    parameter_record.id = parameter_declaration;
    parameter_record.scope = class_scope;
    parameter_record.form = sv::DeclarationForm::parameter;
    parameter_record.name = "COUNT";
    parameter_record.type = builder.systemverilog.types().front().base;
    parameter_record.initializer = class_default;
    parameter_record.source = builder.source;
    parameter_record.origin = parameter_origin;
    builder.systemverilog.mutable_declarations().push_back(
        std::move(parameter_record));
    sv::Declaration property_record;
    property_record.id = property_declaration;
    property_record.scope = class_scope;
    property_record.form = sv::DeclarationForm::variable;
    property_record.name = "payload";
    property_record.type = builder.systemverilog.types().front().base;
    property_record.initializer = class_default;
    property_record.source = builder.source;
    property_record.origin = property_origin;
    builder.systemverilog.mutable_declarations().push_back(
        std::move(property_record));

    sv::ClassDeclaration derived_class;
    derived_class.scope = class_scope;
    derived_class.name = "Derived";
    derived_class.canonical_identity = derived_identity;
    derived_class.enclosing_identity = "work::ConsumerPkg";
    derived_class.base_declaration_identity = "work::Leaf::Base";
    derived_class.source = builder.source;
    derived_class.origin = class_origin;
    sv::ClassRelation base_relation;
    base_relation.name = "Base";
    base_relation.declaration_identity = "work::Leaf::Base";
    base_relation.source = builder.source;
    base_relation.origin = builder.model.add_origin(OriginKind::parsed,
        builder.source, class_origin, "Base");
    sv::ActualAssociation base_actual;
    base_actual.expression = class_default;
    base_actual.source = builder.source;
    base_relation.actuals.push_back(std::move(base_actual));
    derived_class.base = std::move(base_relation);
    sv::ClassParameter class_parameter;
    class_parameter.declaration = parameter_declaration;
    class_parameter.name = "COUNT";
    class_parameter.type
        = builder.systemverilog.types().front().base;
    class_parameter.default_value = class_default;
    class_parameter.source = builder.source;
    class_parameter.origin = parameter_origin;
    derived_class.parameters.push_back(class_parameter);
    sv::ClassProperty class_property;
    class_property.declaration = property_declaration;
    class_property.name = "payload";
    class_property.canonical_identity
        = "work::ConsumerPkg::Derived::payload";
    class_property.owner_identity = derived_class.canonical_identity;
    class_property.type = *class_parameter.type;
    class_property.initializer = class_parameter.default_value;
    class_property.source = builder.source;
    class_property.origin = property_origin;
    derived_class.properties.push_back(std::move(class_property));

    const auto method_origin = builder.model.add_origin(
        OriginKind::parsed, builder.source, class_origin, "deep_reference");
    const auto method_declaration = builder.model.add_declaration(
        class_scope, DeclarationKind::function, "deep_reference",
        builder.source, method_origin);
    const auto method_scope = builder.model.add_scope(
        consumer_package, class_scope, "deep_reference",
        builder.source, method_origin);
    const auto nested_origin = builder.model.add_origin(
        OriginKind::parsed, builder.source, method_origin, "nested");
    const auto nested_declaration = builder.model.add_declaration(
        method_scope, DeclarationKind::function, "nested",
        builder.source, nested_origin);
    const auto nested_scope = builder.model.add_scope(
        consumer_package, method_scope, "nested",
        builder.source, nested_origin);
    const auto leaf_origin = builder.model.add_origin(
        OriginKind::parsed, builder.source, nested_origin, "deep_handle");
    const auto leaf_declaration = builder.model.add_declaration(
        nested_scope, DeclarationKind::variable, "deep_handle",
        builder.source, leaf_origin);
    const auto nested_body_origin = builder.model.add_origin(
        OriginKind::parsed, builder.source, nested_origin, "nested body");
    const auto nested_body = builder.model.add_statement_identity(
        nested_scope, builder.source, nested_body_origin);
    const auto block_scope = builder.model.add_scope(
        consumer_package, nested_scope, "block",
        builder.source, nested_body_origin);
    const auto block_local_origin = builder.model.add_origin(
        OriginKind::parsed, builder.source,
        nested_body_origin, "block_handle");
    const auto block_local_declaration = builder.model.add_declaration(
        block_scope, DeclarationKind::variable, "block_handle",
        builder.source, block_local_origin);

    sv::TypeReference integer_type;
    integer_type.target.source = builder.source;
    integer_type.target.spelling = "int";
    sv::TypeReference base_handle_type;
    base_handle_type.target.source = builder.source;
    base_handle_type.target.spelling = "DeepOnly";
    base_handle_type.value_form = sv::TypeForm::class_handle;
    base_handle_type.class_identity = "work::Leaf::DeepOnly";
    sv::TypeReference block_handle_type;
    block_handle_type.target.source = builder.source;
    block_handle_type.target.spelling = "BlockOnly";
    block_handle_type.value_form = sv::TypeForm::class_handle;
    block_handle_type.class_identity = "work::Leaf::BlockOnly";

    sv::Declaration method_record;
    method_record.id = method_declaration;
    method_record.scope = class_scope;
    method_record.form = sv::DeclarationForm::function;
    method_record.name = "deep_reference";
    method_record.source = builder.source;
    method_record.origin = method_origin;
    method_record.nested_scope = method_scope;
    method_record.callable = sv::CallableProfile {
        true, integer_type, { }, sv::Lifetime::automatic
    };
    method_record.children.push_back(nested_declaration);
    builder.systemverilog.mutable_declarations().push_back(
        std::move(method_record));

    sv::Declaration nested_record;
    nested_record.id = nested_declaration;
    nested_record.scope = method_scope;
    nested_record.form = sv::DeclarationForm::function;
    nested_record.name = "nested";
    nested_record.source = builder.source;
    nested_record.origin = nested_origin;
    nested_record.nested_scope = nested_scope;
    nested_record.callable = sv::CallableProfile {
        true, integer_type, { }, sv::Lifetime::automatic
    };
    nested_record.children.push_back(leaf_declaration);
    nested_record.statements.push_back(nested_body);
    builder.systemverilog.mutable_declarations().push_back(
        std::move(nested_record));

    sv::Declaration leaf_record;
    leaf_record.id = leaf_declaration;
    leaf_record.scope = nested_scope;
    leaf_record.form = sv::DeclarationForm::variable;
    leaf_record.name = "deep_handle";
    leaf_record.source = builder.source;
    leaf_record.origin = leaf_origin;
    leaf_record.type = std::move(base_handle_type);
    builder.systemverilog.mutable_declarations().push_back(
        std::move(leaf_record));

    sv::Declaration block_local_record;
    block_local_record.id = block_local_declaration;
    block_local_record.scope = block_scope;
    block_local_record.form = sv::DeclarationForm::variable;
    block_local_record.name = "block_handle";
    block_local_record.source = builder.source;
    block_local_record.origin = block_local_origin;
    block_local_record.type = std::move(block_handle_type);
    builder.systemverilog.mutable_declarations().push_back(
        std::move(block_local_record));

    sv::Statement nested_body_record;
    nested_body_record.id = nested_body;
    nested_body_record.scope = nested_scope;
    nested_body_record.kind = sv::StatementKind::block;
    nested_body_record.source = builder.source;
    nested_body_record.origin = nested_body_origin;
    nested_body_record.declarations.push_back(block_local_declaration);
    nested_body_record.nested_scope = block_scope;
    builder.systemverilog.mutable_statements().push_back(
        std::move(nested_body_record));

    sv::ClassMethod method;
    method.declaration = method_declaration;
    method.name = "deep_reference";
    method.canonical_identity
        = derived_class.canonical_identity + "::deep_reference";
    method.owner_identity = derived_class.canonical_identity;
    method.profile_identity = "1:deep_reference()->int";
    method.kind = sv::ClassMethodKind::function;
    method.lifetime = sv::ClassLifetime::automatic;
    method.source = builder.source;
    method.origin = method_origin;
    derived_class.methods.push_back(std::move(method));
    derived_class.member_declarations.push_back(parameter_declaration);
    derived_class.member_declarations.push_back(property_declaration);
    derived_class.member_declarations.push_back(method_declaration);
    builder.systemverilog.mutable_classes().push_back(
        std::move(derived_class));
    auto& class_expression
        = builder.systemverilog.mutable_expressions().front();
    class_expression.class_identity = "work::Leaf::Base";
    class_expression.class_checked = true;
    static_cast<void>(consumer_package);

    const auto architecture = builder.add_vhdl_unit(
        UnitKind::vhdl_architecture,
        vhdl::UnitKind::architecture, "Behavioral",
        "ENTITYUNIT", "Work");
    const auto vhdl_owner = builder.add_vhdl_unit(
        UnitKind::vhdl_entity, vhdl::UnitKind::entity,
        "ConsumerVhdl", {}, "Work");
    auto& vhdl_unit = builder.vhdl_unit(vhdl_owner);
    vhdl::ContextItem use_clause;
    use_clause.kind = vhdl::ContextKind::use_clause;
    use_clause.source = builder.source;
    use_clause.selected_names.push_back(
        vhdl_name("WORK.VHDL_PKG.ALL", builder.source));
    vhdl_unit.context.push_back(std::move(use_clause));
    vhdl::ContextItem context_reference;
    context_reference.kind = vhdl::ContextKind::context_reference;
    context_reference.source = builder.source;
    context_reference.selected_names.push_back(
        vhdl_name("work.SharedContext", builder.source));
    vhdl_unit.context.push_back(std::move(context_reference));

    const auto vhdl_scope = builder.model.units()[vhdl_owner.value()].scope;
    const auto add_vhdl_instance = [&](std::string name,
                                       std::string target,
                                       const bool configuration) {
        const auto id = builder.model.add_instance(vhdl_scope,
            name, target, builder.source, builder.origin);
        vhdl::Instance instance;
        instance.id = id;
        instance.scope = vhdl_scope;
        instance.target = vhdl_name(std::move(target), builder.source);
        instance.name = std::move(name);
        instance.configuration = configuration;
        instance.source = builder.source;
        instance.origin = builder.origin;
        builder.vhdl_hir.mutable_instances().push_back(
            std::move(instance));
        vhdl_unit.instances.push_back(id);
    };
    add_vhdl_instance(
        "entity_child", "work.entityunit(gate)", false);
    add_vhdl_instance("configuration_child", "work.vhdlcfg", true);

    const auto package_declaration = builder.model.add_declaration(
        vhdl_scope, DeclarationKind::package_instance,
        "generic_package", builder.source, builder.origin);
    vhdl::Declaration package_record;
    package_record.id = package_declaration;
    package_record.scope = vhdl_scope;
    package_record.form = vhdl::DeclarationForm::package_instance;
    package_record.name = "generic_package";
    package_record.source = builder.source;
    package_record.origin = builder.origin;
    vhdl::PackageProfile package_profile;
    package_profile.template_name = vhdl_name(
        "work.VHDL_PKG", builder.source);
    package_record.package = std::move(package_profile);
    builder.vhdl_hir.mutable_declarations().push_back(
        std::move(package_record));
    vhdl_unit.declarations.push_back(package_declaration);

    vhdl::ComponentConfiguration entity_binding;
    entity_binding.binding.kind = vhdl::BindingKind::entity;
    entity_binding.binding.entity = vhdl_name(
        "WORK.ENTITYUNIT", builder.source);
    entity_binding.binding.architecture = "rtl";
    entity_binding.binding.source = builder.source;
    entity_binding.source = builder.source;
    vhdl_unit.component_configurations.push_back(
        std::move(entity_binding));
    vhdl::BlockConfiguration block;
    block.block = vhdl_name("ConsumerVhdl", builder.source);
    block.source = builder.source;
    vhdl::ComponentConfiguration configuration_binding;
    configuration_binding.binding.kind
        = vhdl::BindingKind::configuration;
    configuration_binding.binding.configuration = vhdl_name(
        "work.vhdlcfg", builder.source);
    configuration_binding.binding.source = builder.source;
    configuration_binding.source = builder.source;
    block.components.push_back(std::move(configuration_binding));
    vhdl_unit.configuration = std::move(block);
    static_cast<void>(architecture);
    return builder.finish();
}

CompiledDesign make_collision_bundle(
    const std::string& unit_name, const std::string& class_identity,
    const std::string& udp_name,
    const Language language = Language::system_verilog)
{
    LinkBundleBuilder builder { unit_name + ".sv" };
    const auto owner = builder.add_systemverilog_unit(UnitKind::verilog_module,
        sv::UnitKind::module, unit_name, "work", language);
    if (!class_identity.empty()) {
        sv::ClassDeclaration declaration;
        declaration.name = "Duplicate";
        declaration.canonical_identity = class_identity;
        declaration.enclosing_identity = "work::" + unit_name;
        declaration.source = builder.source;
        declaration.origin = builder.model.add_origin(OriginKind::parsed,
            builder.source, builder.model.units()[owner.value()].origin,
            declaration.canonical_identity);
        declaration.scope = builder.model.add_scope(owner,
            builder.model.units()[owner.value()].scope, declaration.name,
            declaration.source, declaration.origin);
        builder.systemverilog.mutable_classes().push_back(
            std::move(declaration));
    }
    if (!udp_name.empty()) {
        builder.systemverilog.mutable_udps().push_back(
            valid_udp_declaration(
                udp_name, builder.source, builder.origin));
    }
    return builder.finish();
}

CompiledDesign make_prototype_definition_bundle()
{
    LinkBundleBuilder builder { "prototype-definition.sv" };
    const auto prototype = builder.add_systemverilog_unit(
        UnitKind::verilog_module, sv::UnitKind::module,
        "PrototypeDefinition");
    builder.systemverilog_unit(prototype).external = true;
    builder.add_systemverilog_unit(UnitKind::verilog_module,
        sv::UnitKind::module, "PrototypeDefinition");
    return builder.finish();
}

CompiledDesign make_vhdl_collision_bundle(
    std::string name, std::string library)
{
    LinkBundleBuilder builder { name + ".vhd" };
    builder.add_vhdl_unit(UnitKind::vhdl_entity,
        vhdl::UnitKind::entity, std::move(name), {},
        std::move(library));
    return builder.finish();
}

CompiledDesign make_library_target_bundle(std::string library)
{
    LinkBundleBuilder builder { library + "-shared.sv" };
    builder.add_systemverilog_unit(UnitKind::verilog_module,
        sv::UnitKind::module, "SharedTarget", std::move(library));
    return builder.finish();
}

CompiledDesign make_library_consumer_bundle()
{
    LinkBundleBuilder builder { "library-consumer.sv" };
    const auto consumer = builder.add_systemverilog_unit(
        UnitKind::verilog_module, sv::UnitKind::module,
        "LibraryConsumer");
    builder.add_systemverilog_graph(
        consumer, "selected_lib::SharedTarget", "23");
    return builder.finish();
}

CompiledDesign make_relocation_anchor_bundle()
{
    LinkBundleBuilder builder { "relocation-anchor.vhd" };
    const auto anchor = builder.add_vhdl_unit(UnitKind::vhdl_entity,
        vhdl::UnitKind::entity, "AAnchor");
    builder.add_vhdl_graph(anchor, "AAnchor", "2");
    return builder.finish();
}

CompiledDesign make_systemverilog_auxiliary_record_bundle(
    std::string logical_stem = "zz-auxiliary-records",
    std::string unit_name = "ZZAuxiliaryRecords",
    std::string library = "work")
{
    LinkBundleBuilder builder { logical_stem + ".sv" };
    const auto owner = builder.add_systemverilog_unit(
        UnitKind::verilog_module, sv::UnitKind::module,
        unit_name, library);
    builder.add_systemverilog_graph(
        owner, unit_name, "41");
    const auto header_file = builder.model.intern_source_file(
        "/checkout/" + logical_stem + ".svh",
        logical_stem + "-header-digest");
    const auto header_source = builder.model.intern_source_span(
        header_file, logical_stem + ".svh",
        { 20, 2, 1 }, { 40, 2, 21 });
    const auto header_origin = builder.model.add_origin(
        OriginKind::parsed, header_source, builder.origin,
        logical_stem + " included declarations");
    const auto owner_scope = builder.model.units()[owner.value()].scope;
    const auto owner_identity = library + "::" + unit_name;

    sv::DpiDeclaration dpi;
    dpi.standard = "1800-2023";
    dpi.owner_kind = sv::DpiOwnerKind::design_unit;
    dpi.owner_scope = owner_scope;
    dpi.owner_identity = owner_identity;
    dpi.link_name = unit_name + "_dpi";
    dpi.systemverilog_name = unit_name + "_dpi";
    dpi.profile_source = header_source;
    dpi.linkage_name = unit_name + "_dpi_linkage";
    dpi.source = header_source;
    dpi.origin = header_origin;
    dpi.return_type_tokens.push_back({ 0U, "int", header_source });
    builder.systemverilog.mutable_dpi_declarations().push_back(
        std::move(dpi));

    sv::CovergroupInstance covergroup;
    covergroup.name = unit_name + "_coverage";
    covergroup.owner_scope = owner_scope;
    covergroup.owner_identity = owner_identity;
    covergroup.declaration_identity = owner_identity + "::coverage";
    covergroup.specialization_identity = "default";
    covergroup.runtime_identity = owner_identity + "::coverage#default";
    covergroup.constructor_actuals.push_back(
        builder.systemverilog.expressions().front().id);
    covergroup.source = header_source;
    covergroup.origin = header_origin;
    builder.systemverilog.mutable_covergroup_instances().push_back(
        std::move(covergroup));

    return builder.finish();
}

DeclarationId add_sv_actual(LinkBundleBuilder& builder,
    const UnitId unit, const sv::DeclarationForm form,
    const std::string& name, std::optional<TypeId>& declared_type)
{
    const auto scope = builder.model.units()[unit.value()].scope;
    const auto declaration = builder.model.add_declaration(scope,
        form == sv::DeclarationForm::type_parameter
            ? DeclarationKind::type
            : DeclarationKind::constant,
        name, builder.source, builder.origin);
    sv::Declaration record;
    record.id = declaration;
    record.scope = scope;
    record.form = form;
    record.name = name;
    record.source = builder.source;
    record.origin = builder.origin;
    if (form == sv::DeclarationForm::type_parameter) {
        TypeReference base;
        base.source = builder.source;
        base.spelling = name;
        const auto type = builder.model.add_type(scope,
            TypeKind::declaration, name, base,
            builder.source, builder.origin);
        sv::TypeDefinition type_record;
        type_record.id = type;
        type_record.declaration = declaration;
        type_record.form = sv::TypeForm::type_parameter;
        type_record.name = name;
        type_record.base.target.source = builder.source;
        type_record.base.target.spelling = name;
        type_record.source = builder.source;
        type_record.origin = builder.origin;
        builder.systemverilog.mutable_types().push_back(
            std::move(type_record));
        record.declared_type = type;
        declared_type = type;
    }
    builder.systemverilog.mutable_declarations().push_back(
        std::move(record));
    builder.systemverilog_unit(unit).declarations.push_back(declaration);
    return declaration;
}

DeclarationId add_vhdl_actual(LinkBundleBuilder& builder,
    const UnitId unit, const vhdl::DeclarationForm form,
    const std::string& name, std::optional<TypeId>& declared_type)
{
    const auto scope = builder.model.units()[unit.value()].scope;
    const auto declaration = builder.model.add_declaration(scope,
        form == vhdl::DeclarationForm::generic_type
            ? DeclarationKind::type
            : DeclarationKind::generic,
        name, builder.source, builder.origin);
    vhdl::Declaration record;
    record.id = declaration;
    record.scope = scope;
    record.form = form;
    record.name = name;
    record.source = builder.source;
    record.origin = builder.origin;
    if (form == vhdl::DeclarationForm::generic_type) {
        TypeReference base;
        base.source = builder.source;
        base.spelling = name;
        const auto type = builder.model.add_type(scope,
            TypeKind::declaration, name, base,
            builder.source, builder.origin);
        vhdl::TypeDefinition type_record;
        type_record.id = type;
        type_record.declaration = declaration;
        type_record.name = name;
        type_record.base.type_mark.source = builder.source;
        type_record.source = builder.source;
        type_record.origin = builder.origin;
        builder.vhdl_hir.mutable_types().push_back(
            std::move(type_record));
        record.declared_type = type;
        declared_type = type;
    }
    builder.vhdl_hir.mutable_declarations().push_back(std::move(record));
    builder.vhdl_unit(unit).declarations.push_back(declaration);
    return declaration;
}

ExpressionId add_sv_dependent_expression(LinkBundleBuilder& builder,
    const UnitId unit, const std::string& text,
    ResidualDependencies dependencies)
{
    const auto scope = builder.model.units()[unit.value()].scope;
    const auto id = builder.model.add_expression_identity(
        scope, builder.source, builder.origin);
    sv::Expression expression;
    expression.id = id;
    expression.scope = scope;
    expression.kind = sv::ExpressionKind::name;
    expression.text = text;
    expression.source = builder.source;
    expression.origin = builder.origin;
    expression.dependencies = std::move(dependencies);
    builder.systemverilog.mutable_expressions().push_back(
        std::move(expression));
    return id;
}

ExpressionId add_vhdl_dependent_expression(LinkBundleBuilder& builder,
    const UnitId unit, const std::string& text,
    ResidualDependencies dependencies)
{
    const auto scope = builder.model.units()[unit.value()].scope;
    const auto id = builder.model.add_expression_identity(
        scope, builder.source, builder.origin);
    vhdl::Expression expression;
    expression.id = id;
    expression.scope = scope;
    expression.kind = vhdl::ExpressionKind::name;
    expression.text = text;
    expression.source = builder.source;
    expression.origin = builder.origin;
    expression.dependencies = std::move(dependencies);
    builder.vhdl_hir.mutable_expressions().push_back(
        std::move(expression));
    return id;
}

DeclarationId add_sv_dependent_generate(LinkBundleBuilder& builder,
    const UnitId unit, ResidualDependencies dependencies)
{
    const auto scope = builder.model.units()[unit.value()].scope;
    const auto declaration = builder.model.add_declaration(scope,
        DeclarationKind::generate, "generated",
        builder.source, builder.origin);
    sv::Declaration record;
    record.id = declaration;
    record.scope = scope;
    record.form = sv::DeclarationForm::generated;
    record.name = "generated";
    record.source = builder.source;
    record.origin = builder.origin;
    builder.systemverilog.mutable_declarations().push_back(
        std::move(record));
    builder.systemverilog_unit(unit).declarations.push_back(declaration);
    sv::GenerateRegion generate;
    generate.declaration = declaration;
    generate.scope = scope;
    generate.source = builder.source;
    generate.origin = builder.origin;
    generate.dependencies = std::move(dependencies);
    builder.systemverilog_unit(unit).generates.push_back(
        std::move(generate));
    return declaration;
}

DeclarationId add_vhdl_dependent_generate(LinkBundleBuilder& builder,
    const UnitId unit, ResidualDependencies dependencies)
{
    const auto unit_scope = builder.model.units()[unit.value()].scope;
    const auto declaration = builder.model.add_declaration(unit_scope,
        DeclarationKind::generate, "generated",
        builder.source, builder.origin);
    vhdl::Declaration record;
    record.id = declaration;
    record.scope = unit_scope;
    record.form = vhdl::DeclarationForm::generated;
    record.name = "generated";
    record.source = builder.source;
    record.origin = builder.origin;
    builder.vhdl_hir.mutable_declarations().push_back(std::move(record));
    builder.vhdl_unit(unit).declarations.push_back(declaration);
    vhdl::GenerateRegion generate;
    generate.declaration = declaration;
    generate.scope = builder.model.add_scope(unit, unit_scope,
        "generated", builder.source, builder.origin);
    generate.source = builder.source;
    generate.origin = builder.origin;
    generate.dependencies = std::move(dependencies);
    builder.vhdl_unit(unit).generates.push_back(std::move(generate));
    return declaration;
}

InstanceId add_sv_generate_instance(LinkBundleBuilder& builder,
    const UnitId unit, const DeclarationId generate,
    std::string name)
{
    auto& regions = builder.systemverilog_unit(unit).generates;
    const auto found = std::ranges::find(
        regions, generate, &sv::GenerateRegion::declaration);
    assert(found != regions.end());
    const auto instance = builder.model.add_instance(found->scope,
        name, "ForeignSv", builder.source, builder.origin);
    sv::Instance record;
    record.id = instance;
    record.scope = found->scope;
    record.target.spelling = "ForeignSv";
    record.target.source = builder.source;
    record.name = std::move(name);
    record.source = builder.source;
    record.origin = builder.origin;
    builder.systemverilog.mutable_instances().push_back(
        std::move(record));
    found->instances.push_back(instance);
    return instance;
}

struct GeneratedInstanceSubtree {
    DeclarationId nested_generate;
    InstanceId direct_instance;
    InstanceId nested_instance;
};

GeneratedInstanceSubtree add_sv_generated_instance_subtree(
    LinkBundleBuilder& builder, const UnitId unit,
    const DeclarationId parent_generate)
{
    GeneratedInstanceSubtree result;
    result.direct_instance = add_sv_generate_instance(builder, unit,
        parent_generate, "generated_child");
    auto& regions = builder.systemverilog_unit(unit).generates;
    const auto parent = std::ranges::find(
        regions, parent_generate, &sv::GenerateRegion::declaration);
    assert(parent != regions.end());
    result.nested_generate = builder.model.add_declaration(parent->scope,
        DeclarationKind::generate, "nested_generated",
        builder.source, builder.origin);
    sv::Declaration declaration;
    declaration.id = result.nested_generate;
    declaration.scope = parent->scope;
    declaration.form = sv::DeclarationForm::generated;
    declaration.name = "nested_generated";
    declaration.source = builder.source;
    declaration.origin = builder.origin;
    builder.systemverilog.mutable_declarations().push_back(
        std::move(declaration));
    sv::GenerateRegion nested;
    nested.declaration = result.nested_generate;
    nested.scope = builder.model.add_scope(unit, parent->scope,
        "nested_generated", builder.source, builder.origin);
    nested.source = builder.source;
    nested.origin = builder.origin;
    result.nested_instance = builder.model.add_instance(nested.scope,
        "nested_child", "ForeignSv", builder.source, builder.origin);
    sv::Instance instance;
    instance.id = result.nested_instance;
    instance.scope = nested.scope;
    instance.target.spelling = "ForeignSv";
    instance.target.source = builder.source;
    instance.name = "nested_child";
    instance.source = builder.source;
    instance.origin = builder.origin;
    builder.systemverilog.mutable_instances().push_back(
        std::move(instance));
    nested.instances.push_back(result.nested_instance);
    parent->nested.push_back(std::move(nested));
    return result;
}

InstanceId add_vhdl_generate_instance(LinkBundleBuilder& builder,
    const UnitId unit, const DeclarationId generate,
    std::string name)
{
    auto& regions = builder.vhdl_unit(unit).generates;
    const auto found = std::ranges::find(
        regions, generate, &vhdl::GenerateRegion::declaration);
    assert(found != regions.end());
    const auto instance = builder.model.add_instance(found->scope,
        name, "ForeignEntity", builder.source, builder.origin);
    vhdl::Instance record;
    record.id = instance;
    record.scope = found->scope;
    record.target.spelling = "ForeignEntity";
    record.target.canonical = "ForeignEntity";
    record.target.source = builder.source;
    record.name = std::move(name);
    record.source = builder.source;
    record.origin = builder.origin;
    builder.vhdl_hir.mutable_instances().push_back(std::move(record));
    found->instances.push_back(instance);
    return instance;
}

GeneratedInstanceSubtree add_vhdl_generated_instance_subtree(
    LinkBundleBuilder& builder, const UnitId unit,
    const DeclarationId parent_generate)
{
    GeneratedInstanceSubtree result;
    result.direct_instance = add_vhdl_generate_instance(builder, unit,
        parent_generate, "generated_child");
    auto& regions = builder.vhdl_unit(unit).generates;
    const auto parent = std::ranges::find(
        regions, parent_generate, &vhdl::GenerateRegion::declaration);
    assert(parent != regions.end());
    result.nested_generate = builder.model.add_declaration(parent->scope,
        DeclarationKind::generate, "nested_generated",
        builder.source, builder.origin);
    vhdl::Declaration declaration;
    declaration.id = result.nested_generate;
    declaration.scope = parent->scope;
    declaration.form = vhdl::DeclarationForm::generated;
    declaration.name = "nested_generated";
    declaration.source = builder.source;
    declaration.origin = builder.origin;
    builder.vhdl_hir.mutable_declarations().push_back(
        std::move(declaration));
    vhdl::GenerateRegion nested;
    nested.declaration = result.nested_generate;
    nested.scope = builder.model.add_scope(unit, parent->scope,
        "nested_generated", builder.source, builder.origin);
    nested.source = builder.source;
    nested.origin = builder.origin;
    result.nested_instance = builder.model.add_instance(nested.scope,
        "nested_child", "ForeignEntity", builder.source, builder.origin);
    vhdl::Instance instance;
    instance.id = result.nested_instance;
    instance.scope = nested.scope;
    instance.target.spelling = "ForeignEntity";
    instance.target.canonical = "ForeignEntity";
    instance.target.source = builder.source;
    instance.name = "nested_child";
    instance.source = builder.source;
    instance.origin = builder.origin;
    builder.vhdl_hir.mutable_instances().push_back(std::move(instance));
    nested.instances.push_back(result.nested_instance);
    parent->nested.push_back(std::move(nested));
    return result;
}

struct SpecializationFixture {
    CompiledDesign design;
    UnitId systemverilog_unit;
    UnitId vhdl_entity;
    UnitId vhdl_architecture;
    DeclarationId parameter;
    DeclarationId type_parameter;
    DeclarationId local_parameter;
    DeclarationId foreign_parameter;
    DeclarationId entity_generic;
    DeclarationId entity_type_generic;
    DeclarationId entity_function_generic;
    DeclarationId entity_procedure_generic;
    DeclarationId entity_package_generic;
    DeclarationId architecture_generic;
    DeclarationId foreign_generic;
    ExpressionId parameter_expression;
    ExpressionId type_expression;
    ExpressionId local_expression;
    ExpressionId entity_expression;
    ExpressionId entity_type_expression;
    ExpressionId entity_nonvalue_expression;
    ExpressionId architecture_expression;
    DeclarationId parameter_generate;
    DeclarationId type_generate;
    DeclarationId entity_generate;
    DeclarationId architecture_generate;
    DeclarationId nested_systemverilog_generate;
    DeclarationId nested_vhdl_generate;
    InstanceId parameter_generate_instance;
    InstanceId nested_systemverilog_instance;
    InstanceId type_generate_instance;
    InstanceId entity_generate_instance;
    InstanceId nested_vhdl_instance;
    InstanceId architecture_generate_instance;
};

SpecializationFixture make_specialization_fixture()
{
    LinkBundleBuilder builder { "specialization.hdl" };
    SpecializationFixture fixture;
    fixture.systemverilog_unit = builder.add_systemverilog_unit(
        UnitKind::verilog_module, sv::UnitKind::module, "SpecializedSv");
    const auto foreign_sv = builder.add_systemverilog_unit(
        UnitKind::verilog_module, sv::UnitKind::module, "ForeignSv");
    std::optional<TypeId> type_parameter_type;
    std::optional<TypeId> unused;
    fixture.parameter = add_sv_actual(builder,
        fixture.systemverilog_unit, sv::DeclarationForm::parameter,
        "P", unused);
    fixture.type_parameter = add_sv_actual(builder,
        fixture.systemverilog_unit, sv::DeclarationForm::type_parameter,
        "T", type_parameter_type);
    fixture.local_parameter = add_sv_actual(builder,
        fixture.systemverilog_unit,
        sv::DeclarationForm::local_parameter, "LOCAL", unused);
    fixture.foreign_parameter = add_sv_actual(builder, foreign_sv,
        sv::DeclarationForm::parameter, "FOREIGN", unused);
    ResidualDependencies parameter_dependencies;
    parameter_dependencies.parameters.push_back(fixture.parameter);
    fixture.parameter_expression = add_sv_dependent_expression(builder,
        fixture.systemverilog_unit, "P", parameter_dependencies);
    ResidualDependencies type_dependencies;
    assert(type_parameter_type);
    type_dependencies.types.push_back(*type_parameter_type);
    fixture.type_expression = add_sv_dependent_expression(builder,
        fixture.systemverilog_unit, "T", type_dependencies);
    ResidualDependencies local_dependencies;
    local_dependencies.parameters.push_back(fixture.local_parameter);
    fixture.local_expression = add_sv_dependent_expression(builder,
        fixture.systemverilog_unit, "LOCAL", local_dependencies);
    ResidualDependencies foreign_parameter_dependencies;
    foreign_parameter_dependencies.parameters.push_back(
        fixture.foreign_parameter);
    add_sv_dependent_expression(builder, fixture.systemverilog_unit,
        "FOREIGN", foreign_parameter_dependencies);
    fixture.parameter_generate = add_sv_dependent_generate(builder,
        fixture.systemverilog_unit, parameter_dependencies);
    fixture.type_generate = add_sv_dependent_generate(builder,
        fixture.systemverilog_unit, type_dependencies);
    builder.add_systemverilog_graph(
        fixture.systemverilog_unit, "ForeignSv", "5");
    const auto sv_generated = add_sv_generated_instance_subtree(builder,
        fixture.systemverilog_unit, fixture.parameter_generate);
    fixture.nested_systemverilog_generate
        = sv_generated.nested_generate;
    fixture.parameter_generate_instance
        = sv_generated.direct_instance;
    fixture.nested_systemverilog_instance
        = sv_generated.nested_instance;
    fixture.type_generate_instance = add_sv_generate_instance(builder,
        fixture.systemverilog_unit, fixture.type_generate,
        "type_generated_child");

    fixture.vhdl_entity = builder.add_vhdl_unit(UnitKind::vhdl_entity,
        vhdl::UnitKind::entity, "Entity", {}, "Work");
    fixture.vhdl_architecture = builder.add_vhdl_unit(
        UnitKind::vhdl_architecture, vhdl::UnitKind::architecture,
        "RTL", "ENTITY", "work");
    const auto foreign_entity = builder.add_vhdl_unit(
        UnitKind::vhdl_entity, vhdl::UnitKind::entity,
        "ForeignEntity", {}, "work");
    std::optional<TypeId> entity_type;
    fixture.entity_generic = add_vhdl_actual(builder,
        fixture.vhdl_entity, vhdl::DeclarationForm::generic_constant,
        "G", unused);
    fixture.entity_type_generic = add_vhdl_actual(builder,
        fixture.vhdl_entity, vhdl::DeclarationForm::generic_type,
        "GT", entity_type);
    fixture.entity_function_generic = add_vhdl_actual(builder,
        fixture.vhdl_entity, vhdl::DeclarationForm::generic_function,
        "GF", unused);
    fixture.entity_procedure_generic = add_vhdl_actual(builder,
        fixture.vhdl_entity, vhdl::DeclarationForm::generic_procedure,
        "GP", unused);
    fixture.entity_package_generic = add_vhdl_actual(builder,
        fixture.vhdl_entity, vhdl::DeclarationForm::generic_package,
        "PKG", unused);
    fixture.architecture_generic = add_vhdl_actual(builder,
        fixture.vhdl_architecture,
        vhdl::DeclarationForm::generic_constant, "AG", unused);
    fixture.foreign_generic = add_vhdl_actual(builder, foreign_entity,
        vhdl::DeclarationForm::generic_constant, "FG", unused);
    ResidualDependencies entity_dependencies;
    entity_dependencies.generics.push_back(fixture.entity_generic);
    fixture.entity_expression = add_vhdl_dependent_expression(builder,
        fixture.vhdl_entity, "G", entity_dependencies);
    ResidualDependencies entity_type_dependencies;
    assert(entity_type);
    entity_type_dependencies.types.push_back(*entity_type);
    fixture.entity_type_expression = add_vhdl_dependent_expression(
        builder, fixture.vhdl_entity, "GT", entity_type_dependencies);
    ResidualDependencies entity_nonvalue_dependencies;
    entity_nonvalue_dependencies.generics = {
        fixture.entity_function_generic,
        fixture.entity_procedure_generic,
        fixture.entity_package_generic,
    };
    fixture.entity_nonvalue_expression = add_vhdl_dependent_expression(
        builder, fixture.vhdl_entity, "GF(GP(PKG))",
        entity_nonvalue_dependencies);
    ResidualDependencies architecture_dependencies;
    architecture_dependencies.generics.push_back(
        fixture.architecture_generic);
    fixture.architecture_expression = add_vhdl_dependent_expression(
        builder, fixture.vhdl_architecture, "AG",
        architecture_dependencies);
    ResidualDependencies foreign_generic_dependencies;
    foreign_generic_dependencies.generics.push_back(
        fixture.foreign_generic);
    add_vhdl_dependent_expression(builder,
        fixture.vhdl_architecture, "FG",
        foreign_generic_dependencies);
    fixture.entity_generate = add_vhdl_dependent_generate(builder,
        fixture.vhdl_entity, entity_dependencies);
    fixture.architecture_generate = add_vhdl_dependent_generate(builder,
        fixture.vhdl_architecture, architecture_dependencies);
    builder.add_vhdl_graph(
        fixture.vhdl_entity, "ForeignEntity", "17");
    builder.add_vhdl_graph(
        fixture.vhdl_architecture, "Entity", "19");
    const auto vhdl_generated = add_vhdl_generated_instance_subtree(builder,
        fixture.vhdl_entity, fixture.entity_generate);
    fixture.nested_vhdl_generate = vhdl_generated.nested_generate;
    fixture.entity_generate_instance = vhdl_generated.direct_instance;
    fixture.nested_vhdl_instance = vhdl_generated.nested_instance;
    fixture.architecture_generate_instance = add_vhdl_generate_instance(
        builder, fixture.vhdl_architecture,
        fixture.architecture_generate, "architecture_generated_child");
    fixture.design = builder.finish();
    return fixture;
}

template <typename Record, typename Identity, typename Predicate>
const Record& record_in_unit(const CompiledDesign& design,
    const std::vector<Record>& records,
    const std::vector<Identity>& identities, const UnitId unit,
    Predicate predicate)
{
    const auto found = std::ranges::find_if(records, [&](const auto& record) {
        if (!record.id.valid() || record.id.value() >= identities.size()) {
            return false;
        }
        const auto scope = identities[record.id.value()].scope;
        return scope.valid() && scope.value() < design.semantics.scopes().size()
            && design.semantics.scopes()[scope.value()].unit == unit
            && predicate(record);
    });
    assert(found != records.end());
    return *found;
}

sv::Expression systemverilog_expression(
    const CompiledDesign& design, const std::size_t index,
    const sv::ExpressionKind kind, std::string text,
    std::vector<ExpressionId> operands = {})
{
    const auto& unit = design.systemverilog_units().front();
    sv::Expression result;
    result.id = ExpressionId::from_index(
        static_cast<std::uint32_t>(index));
    result.scope = unit.scope;
    result.kind = kind;
    result.text = std::move(text);
    result.source = unit.source;
    result.origin = unit.origin;
    result.operands = std::move(operands);
    return result;
}

vhdl::Expression vhdl_expression(
    const CompiledDesign& design, const std::size_t index,
    const vhdl::ExpressionKind kind, std::string text,
    std::vector<ExpressionId> operands = {})
{
    const auto& unit = design.vhdl_units().front();
    vhdl::Expression result;
    result.id = ExpressionId::from_index(
        static_cast<std::uint32_t>(index));
    result.scope = unit.scope;
    result.kind = kind;
    result.text = std::move(text);
    result.source = unit.source;
    result.origin = unit.origin;
    result.operands = std::move(operands);
    return result;
}

template <typename Expression>
void assert_same_expressions(
    const std::vector<Expression>& left,
    const std::vector<Expression>& right)
{
    assert(left.size() == right.size());
    for (std::size_t index = 0; index < left.size(); ++index) {
        assert(left[index].id == right[index].id);
        assert(left[index].scope == right[index].scope);
        assert(left[index].kind == right[index].kind);
        assert(left[index].text == right[index].text);
        assert(left[index].source == right[index].source);
        assert(left[index].origin == right[index].origin);
        assert(left[index].operands == right[index].operands);
        assert(left[index].dependencies == right[index].dependencies);
        assert(left[index].folded == right[index].folded);
        if constexpr (requires(const Expression& expression) {
                          expression.builtin_operator;
                      }) {
            assert(left[index].builtin_operator
                == right[index].builtin_operator);
        }
    }
}

CompiledLinkResult link_pair(
    CompiledDesign first, CompiledDesign second)
{
    std::vector<CompiledDesign> inputs;
    inputs.push_back(std::move(first));
    inputs.push_back(std::move(second));
    return link_compiled_designs(std::move(inputs));
}

const Unit& referenced_unit(
    const CompiledDesign& design, const CompiledReference& reference)
{
    assert(reference.target);
    assert(reference.target->value() < design.units().size());
    return design.units()[reference.target->value()];
}

bool has_reference(const CompiledDesign& design,
    const CompiledReferenceKind kind, const std::string_view owner,
    const UnitKind target_kind, const std::string_view target_name)
{
    return std::ranges::any_of(design.references(), [&](const auto& reference) {
        if (reference.kind != kind || !reference.owner.valid()
            || reference.owner.value() >= design.units().size()
            || design.units()[reference.owner.value()].name != owner
            || !reference.target) {
            return false;
        }
        const auto& target = referenced_unit(design, reference);
        return target.kind == target_kind && target.name == target_name;
    });
}

std::string qualified_instance_identity(
    const CompiledDesign& design, const InstanceId id)
{
    assert(id.valid() && id.value() < design.semantics.instances().size());
    const auto& instance = design.semantics.instances()[id.value()];
    std::vector<std::string_view> scopes;
    auto scope = instance.scope;
    while (scope.valid()) {
        assert(scope.value() < design.semantics.scopes().size());
        const auto& record = design.semantics.scopes()[scope.value()];
        scopes.push_back(record.name);
        if (!record.parent) {
            break;
        }
        scope = *record.parent;
    }
    std::string result;
    for (auto current = scopes.rbegin(); current != scopes.rend(); ++current) {
        if (current->empty()) {
            continue;
        }
        if (!result.empty()) {
            result += '.';
        }
        result += *current;
    }
    if (!result.empty()) {
        result += '.';
    }
    result += instance.name;
    return result;
}

const Instance& semantic_instance(
    const CompiledDesign& design, const std::string_view name,
    const Language language)
{
    const auto found = std::ranges::find_if(
        design.semantics.instances(), [&](const Instance& instance) {
            if (instance.name != name
                || !instance.scope.valid()
                || instance.scope.value() >= design.semantics.scopes().size()) {
                return false;
            }
            const auto unit
                = design.semantics.scopes()[instance.scope.value()].unit;
            return unit.valid() && unit.value() < design.units().size()
                && design.units()[unit.value()].language == language;
        });
    assert(found != design.semantics.instances().end());
    return *found;
}

void assert_same_link_product(
    const CompiledDesign& left, const CompiledDesign& right)
{
    assert(std::ranges::equal(
        left.dependencies(), right.dependencies()));
    assert(std::ranges::equal(left.references(), right.references()));
    assert(left.units().size() == right.units().size());
    assert(left.semantics.source_files().size()
        == right.semantics.source_files().size());
    assert(left.semantics.source_spans().size()
        == right.semantics.source_spans().size());
    assert(left.semantics.origins().size()
        == right.semantics.origins().size());
    assert(left.semantics.scopes().size()
        == right.semantics.scopes().size());
    assert(left.semantics.instances().size()
        == right.semantics.instances().size());
    for (std::size_t index = 0;
         index < left.semantics.source_files().size(); ++index) {
        const auto& lhs = left.semantics.source_files()[index];
        const auto& rhs = right.semantics.source_files()[index];
        assert(lhs.id == rhs.id);
        assert(lhs.physical_name == rhs.physical_name);
        assert(lhs.content_digest == rhs.content_digest);
    }
    for (std::size_t index = 0;
         index < left.semantics.source_spans().size(); ++index) {
        const auto& lhs = left.semantics.source_spans()[index];
        const auto& rhs = right.semantics.source_spans()[index];
        assert(lhs.id == rhs.id);
        assert(lhs.file == rhs.file);
        assert(lhs.logical_name == rhs.logical_name);
        assert(lhs.begin == rhs.begin);
        assert(lhs.end == rhs.end);
        assert(lhs.expansion == rhs.expansion);
    }
    for (std::size_t index = 0;
         index < left.semantics.origins().size(); ++index) {
        const auto& lhs = left.semantics.origins()[index];
        const auto& rhs = right.semantics.origins()[index];
        assert(lhs.id == rhs.id);
        assert(lhs.kind == rhs.kind);
        assert(lhs.source == rhs.source);
        assert(lhs.parent == rhs.parent);
        assert(lhs.detail == rhs.detail);
    }
    for (std::size_t index = 0;
         index < left.semantics.scopes().size(); ++index) {
        const auto& lhs = left.semantics.scopes()[index];
        const auto& rhs = right.semantics.scopes()[index];
        assert(lhs.id == rhs.id);
        assert(lhs.unit == rhs.unit);
        assert(lhs.parent == rhs.parent);
        assert(lhs.name == rhs.name);
        assert(lhs.source == rhs.source);
        assert(lhs.origin == rhs.origin);
    }
    for (std::size_t index = 0; index < left.units().size(); ++index) {
        const auto& lhs = left.units()[index];
        const auto& rhs = right.units()[index];
        assert(lhs.id == rhs.id);
        assert(lhs.scope == rhs.scope);
        assert(lhs.language == rhs.language);
        assert(lhs.kind == rhs.kind);
        assert(lhs.library == rhs.library);
        assert(lhs.name == rhs.name);
        assert(lhs.secondary_name == rhs.secondary_name);
        assert(lhs.source == rhs.source);
        assert(lhs.origin == rhs.origin);
    }
    for (std::size_t index = 0;
         index < left.semantics.instances().size(); ++index) {
        const auto& lhs = left.semantics.instances()[index];
        const auto& rhs = right.semantics.instances()[index];
        assert(lhs.id == rhs.id);
        assert(lhs.scope == rhs.scope);
        assert(lhs.name == rhs.name);
        assert(lhs.target == rhs.target);
        assert(lhs.source == rhs.source);
        assert(lhs.origin == rhs.origin);
    }
    const auto compare_ids = [](const auto& lhs, const auto& rhs) {
        assert(lhs.size() == rhs.size());
        for (std::size_t index = 0; index < lhs.size(); ++index) {
            assert(lhs[index].id == rhs[index].id);
            assert(lhs[index].scope == rhs[index].scope);
            assert(lhs[index].source == rhs[index].source);
            assert(lhs[index].origin == rhs[index].origin);
        }
    };
    compare_ids(left.systemverilog_hir.declarations(),
        right.systemverilog_hir.declarations());
    compare_ids(left.systemverilog_hir.expressions(),
        right.systemverilog_hir.expressions());
    compare_ids(left.systemverilog_hir.statements(),
        right.systemverilog_hir.statements());
    compare_ids(left.systemverilog_hir.processes(),
        right.systemverilog_hir.processes());
    compare_ids(left.systemverilog_hir.instances(),
        right.systemverilog_hir.instances());
    compare_ids(left.vhdl_hir.declarations(),
        right.vhdl_hir.declarations());
    compare_ids(left.vhdl_hir.instances(),
        right.vhdl_hir.instances());
}

void test_compiled_udp_validation()
{
    const auto source = SourceSpanId::from_index(0);
    const auto origin = OriginId::from_index(0);
    const auto valid = valid_udp_declaration("valid_udp", source, origin);
    assert(sv::udp_declaration_well_formed(valid));

    auto sequential = valid;
    sequential.sequential = true;
    sequential.output_register = true;
    sequential.initial_output = sv::UdpOutput::zero;
    sequential.rows.front().current_state = sv::UdpLevel::dont_care;
    sequential.rows.front().inputs.front().edge = sv::UdpEdge::rising;
    assert(sv::udp_declaration_well_formed(sequential));

    auto invalid = valid;
    invalid.standard = "future";
    assert(!sv::udp_declaration_well_formed(invalid));
    invalid = valid;
    invalid.inputs.front() = invalid.output;
    assert(!sv::udp_declaration_well_formed(invalid));
    invalid = valid;
    invalid.rows.front().inputs.clear();
    assert(!sv::udp_declaration_well_formed(invalid));
    invalid = valid;
    invalid.rows.front().inputs.front().edge = sv::UdpEdge::any;
    assert(!sv::udp_declaration_well_formed(invalid));
    invalid = valid;
    invalid.rows.front().output = sv::UdpOutput::no_change;
    assert(!sv::udp_declaration_well_formed(invalid));
    invalid = valid;
    invalid.rows.push_back(invalid.rows.front());
    assert(!sv::udp_declaration_well_formed(invalid));
    invalid = sequential;
    invalid.output_register = false;
    assert(!sv::udp_declaration_well_formed(invalid));
    invalid = sequential;
    invalid.initial_output = sv::UdpOutput::no_change;
    assert(!sv::udp_declaration_well_formed(invalid));
    invalid = valid;
    invalid.rows.front().output = static_cast<sv::UdpOutput>(255);
    assert(!sv::udp_declaration_well_formed(invalid));
    invalid = valid;
    invalid.rows.front().inputs.front().source = SourceSpanId { };
    assert(!sv::udp_declaration_well_formed(invalid));

    assert(!sv::udp_table_within_resource_budget(
        std::numeric_limits<std::size_t>::max(), 1));
    assert(!sv::udp_table_within_resource_budget(
        1, std::numeric_limits<std::size_t>::max()));

    auto invalid_design = make_link_provider();
    invalid_design.mutable_systemverilog().mutable_udps().front()
        .rows.front().inputs.clear();
    assert(!invalid_design.valid());
    std::vector<CompiledDesign> inputs;
    inputs.push_back(std::move(invalid_design));
    const auto rejected = link_compiled_designs(std::move(inputs));
    assert(!rejected.ok());
    assert(rejected.error.find("structurally invalid")
        != std::string::npos);
}

void test_compiled_design_linker()
{
    const auto reject_class_closure = [](CompiledDesign invalid,
                                          const std::string_view reason) {
        std::vector<CompiledDesign> inputs;
        inputs.push_back(std::move(invalid));
        const auto result = link_compiled_designs(std::move(inputs));
        assert(!result.ok());
        assert(result.error.find(reason) != std::string::npos);
    };
    auto orphaned_nested_body = make_link_consumer();
    auto nested_callable = std::ranges::find(
        orphaned_nested_body.mutable_systemverilog()
            .mutable_declarations(),
        std::string { "nested" }, &sv::Declaration::name);
    assert(nested_callable
        != orphaned_nested_body.mutable_systemverilog()
               .mutable_declarations().end());
    assert(!nested_callable->statements.empty());
    nested_callable->statements.clear();
    reject_class_closure(
        std::move(orphaned_nested_body), "class declaration is orphaned");

    auto missing_nested_body = make_link_consumer();
    nested_callable = std::ranges::find(
        missing_nested_body.mutable_systemverilog()
            .mutable_declarations(),
        std::string { "nested" }, &sv::Declaration::name);
    assert(nested_callable
        != missing_nested_body.mutable_systemverilog()
               .mutable_declarations().end());
    const auto nested_statement = nested_callable->statements.front();
    std::erase_if(
        missing_nested_body.mutable_systemverilog().mutable_statements(),
        [&](const sv::Statement& statement) {
            return statement.id == nested_statement;
        });
    reject_class_closure(
        std::move(missing_nested_body), "declaration/body closure");

    auto orphaned_block_local = make_link_consumer();
    auto block_statement = std::ranges::find_if(
        orphaned_block_local.mutable_systemverilog().mutable_statements(),
        [](const sv::Statement& statement) {
            return !statement.declarations.empty();
        });
    assert(block_statement
        != orphaned_block_local.mutable_systemverilog()
               .mutable_statements().end());
    block_statement->declarations.clear();
    reject_class_closure(
        std::move(orphaned_block_local), "declaration is orphaned");

    auto linked = link_pair(make_link_provider(), make_link_consumer());
    assert(linked.ok());
    assert(linked.design);
    const auto& design = *linked.design;
    assert(design.valid());

    auto wrong_class_owner = make_link_consumer();
    wrong_class_owner.mutable_systemverilog()
        .mutable_classes()
        .front()
        .properties
        .front()
        .owner_identity = "work::wrong";
    std::vector<CompiledDesign> wrong_class_owner_inputs;
    wrong_class_owner_inputs.push_back(std::move(wrong_class_owner));
    const auto rejected_class_owner = link_compiled_designs(
        std::move(wrong_class_owner_inputs));
    assert(!rejected_class_owner.ok());
    assert(rejected_class_owner.error.find("property ownership")
        != std::string::npos);

    auto wrong_class_scope = make_link_consumer();
    const auto consumer_package = wrong_class_scope.find_unit(
        UnitKind::systemverilog_package, "work", "ConsumerPkg");
    assert(consumer_package);
    wrong_class_scope.mutable_systemverilog()
        .mutable_classes()
        .front()
        .scope = consumer_package->identity->scope;
    std::vector<CompiledDesign> wrong_class_scope_inputs;
    wrong_class_scope_inputs.push_back(std::move(wrong_class_scope));
    const auto rejected_class_scope = link_compiled_designs(
        std::move(wrong_class_scope_inputs));
    assert(!rejected_class_scope.ok());
    assert(rejected_class_scope.error.find("class scope")
        != std::string::npos);

    auto orphan_vhdl_instance = make_link_consumer();
    const auto orphan_owner = std::ranges::find_if(
        orphan_vhdl_instance.mutable_vhdl().mutable_units(),
        [](const auto& unit) {
            return unit.kind == vhdl::UnitKind::entity
                && unit.name == "ConsumerVhdl";
        });
    assert(orphan_owner
        != orphan_vhdl_instance.mutable_vhdl().mutable_units().end());
    orphan_owner->instances.clear();
    std::vector<CompiledDesign> orphan_inputs;
    orphan_inputs.push_back(std::move(orphan_vhdl_instance));
    const auto rejected_orphan = link_compiled_designs(
        std::move(orphan_inputs));
    assert(!rejected_orphan.ok());
    assert(rejected_orphan.error.find("not claimed exactly once")
        != std::string::npos);

    auto duplicate_vhdl_instance = make_link_consumer();
    const auto duplicate_owner = std::ranges::find_if(
        duplicate_vhdl_instance.mutable_vhdl().mutable_units(),
        [](const auto& unit) {
            return unit.kind == vhdl::UnitKind::entity
                && unit.name == "ConsumerVhdl";
        });
    assert(duplicate_owner
        != duplicate_vhdl_instance.mutable_vhdl().mutable_units().end());
    duplicate_owner->instances.push_back(
        duplicate_owner->instances.front());
    std::vector<CompiledDesign> duplicate_inputs;
    duplicate_inputs.push_back(std::move(duplicate_vhdl_instance));
    const auto rejected_duplicate = link_compiled_designs(
        std::move(duplicate_inputs));
    assert(!rejected_duplicate.ok());
    assert(rejected_duplicate.error.find("claimed more than once")
        != std::string::npos);

    auto foreign_vhdl_instance = make_link_consumer();
    auto& foreign_units = foreign_vhdl_instance.mutable_vhdl().mutable_units();
    const auto original_owner = std::ranges::find_if(
        foreign_units,
        [](const auto& unit) {
            return unit.kind == vhdl::UnitKind::entity
                && unit.name == "ConsumerVhdl";
        });
    const auto foreign_owner = std::ranges::find_if(
        foreign_units,
        [](const auto& unit) {
            return unit.kind == vhdl::UnitKind::architecture;
        });
    assert(original_owner != foreign_units.end());
    assert(foreign_owner != foreign_units.end());
    foreign_owner->instances.push_back(original_owner->instances.front());
    original_owner->instances.erase(original_owner->instances.begin());
    std::vector<CompiledDesign> foreign_inputs;
    foreign_inputs.push_back(std::move(foreign_vhdl_instance));
    const auto rejected_foreign = link_compiled_designs(
        std::move(foreign_inputs));
    assert(!rejected_foreign.ok());
    assert(rejected_foreign.error.find("foreign unit or generate scope")
        != std::string::npos);

    assert(design.semantics.source_files().size() == 2);
    assert(design.semantics.source_spans().size() == 2);
    assert(design.semantics.origins().size() >= 2);

    auto divergent_origin = make_link_consumer();
    auto& retained_instance
        = divergent_origin.mutable_systemverilog()
              .mutable_instances()
              .front();
    const auto semantic_origin
        = divergent_origin.semantics.instances()
              .at(retained_instance.id.value())
              .origin;
    retained_instance.origin = divergent_origin.semantics.add_origin(
        OriginKind::generated, retained_instance.source,
        semantic_origin, "HIR generate occurrence");
    assert(retained_instance.origin != semantic_origin);
    const auto projected_divergent_origin
        = extract_compiled_library(divergent_origin, "work");
    assert(projected_divergent_origin.ok());
    std::vector<CompiledDesign> divergent_origin_inputs;
    divergent_origin_inputs.push_back(std::move(divergent_origin));
    assert(link_compiled_designs(
        std::move(divergent_origin_inputs)).ok());

    assert(has_reference(design, CompiledReferenceKind::import,
        "Consumer", UnitKind::systemverilog_package, "Leaf"));
    assert(has_reference(design, CompiledReferenceKind::package,
        "Consumer", UnitKind::systemverilog_package, "Leaf"));
    assert(has_reference(design, CompiledReferenceKind::module,
        "Consumer", UnitKind::verilog_module, "Leaf"));
    assert(has_reference(design, CompiledReferenceKind::module,
        "Consumer", UnitKind::systemverilog_interface, "Iface"));
    assert(has_reference(design, CompiledReferenceKind::module,
        "Consumer", UnitKind::systemverilog_program, "ProgramUnit"));
    assert(has_reference(design, CompiledReferenceKind::bind,
        "Consumer", UnitKind::verilog_module, "Leaf"));
    assert(has_reference(design, CompiledReferenceKind::module,
        "SvConfig", UnitKind::verilog_module, "Leaf"));
    assert(has_reference(design,
        CompiledReferenceKind::class_declaration,
        "ConsumerPkg", UnitKind::systemverilog_package, "Leaf"));
    assert(std::ranges::any_of(
        design.references(), [&](const CompiledReference& reference) {
            return reference.kind
                    == CompiledReferenceKind::class_declaration
                && reference.secondary_name == "work::Leaf::DeepOnly"
                && reference.target
                && design.semantics.units()[reference.target->value()].name
                    == "Leaf";
        }));
    assert(std::ranges::any_of(
        design.references(), [&](const CompiledReference& reference) {
            return reference.kind
                    == CompiledReferenceKind::class_declaration
                && reference.secondary_name == "work::Leaf::BlockOnly"
                && reference.target
                && design.semantics.units()[reference.target->value()].name
                    == "Leaf";
        }));
    assert(has_reference(design,
        CompiledReferenceKind::class_declaration,
        "Consumer", UnitKind::systemverilog_package, "Leaf"));

    assert(has_reference(design, CompiledReferenceKind::entity,
        "Behavioral", UnitKind::vhdl_entity, "EntityUnit"));
    assert(has_reference(design, CompiledReferenceKind::entity,
        "ConsumerVhdl", UnitKind::vhdl_entity, "EntityUnit"));
    assert(has_reference(design, CompiledReferenceKind::architecture,
        "ConsumerVhdl", UnitKind::vhdl_architecture, "RTL"));
    assert(has_reference(design, CompiledReferenceKind::architecture,
        "ConsumerVhdl", UnitKind::vhdl_architecture, "Gate"));
    assert(has_reference(design, CompiledReferenceKind::configuration,
        "ConsumerVhdl", UnitKind::vhdl_configuration, "VhdlCfg"));
    assert(has_reference(design, CompiledReferenceKind::package,
        "ConsumerVhdl", UnitKind::vhdl_package, "VHDL_PKG"));
    assert(has_reference(design, CompiledReferenceKind::context,
        "ConsumerVhdl", UnitKind::vhdl_context, "SharedContext"));
    assert(has_reference(design, CompiledReferenceKind::package,
        "SharedContext", UnitKind::vhdl_package, "VHDL_PKG"));

    const auto consumer_unit = design.find_unit(
        UnitKind::verilog_module, "work", "Consumer");
    assert(consumer_unit);
    const auto consumer_expression = std::ranges::find(
        design.systemverilog_hir.expressions(), std::string { "42" },
        &sv::Expression::text);
    assert(consumer_expression != design.systemverilog_hir.expressions().end());
    assert(consumer_expression->id.valid());
    assert(consumer_expression->scope.valid());
    assert(consumer_expression->source == consumer_unit->identity->source);
    assert(consumer_expression->origin
        == design.semantics.expression_identities()
               .at(consumer_expression->id.value())
               .origin);
    const auto expression_view = design.find_expression(
        consumer_expression->id);
    assert(expression_view && *expression_view);
    assert(expression_view->systemverilog == &*consumer_expression);
    assert(expression_view->vhdl == nullptr);
    const auto consumer_statement = std::ranges::find_if(
        design.systemverilog_hir.statements(), [&](const auto& statement) {
            return statement.value
                && *statement.value == consumer_expression->id;
        });
    assert(consumer_statement != design.systemverilog_hir.statements().end());
    assert(consumer_statement->id.valid());
    const auto statement_view = design.find_statement(
        consumer_statement->id);
    assert(statement_view && *statement_view);
    assert(statement_view->systemverilog == &*consumer_statement);
    const auto consumer_process = std::ranges::find(
        design.systemverilog_hir.processes(),
        consumer_statement->id, [](const sv::Process& process) {
            return process.statements.empty()
                ? StatementId { }
                : process.statements.front();
        });
    assert(consumer_process != design.systemverilog_hir.processes().end());
    assert(consumer_process->id.valid());
    const auto process_view = design.find_process(consumer_process->id);
    assert(process_view && *process_view);
    assert(process_view->systemverilog == &*consumer_process);
    const auto consumer_instance = std::ranges::find_if(
        design.systemverilog_hir.instances(), [&](const auto& instance) {
            return instance.name == "child"
                && instance.scope == consumer_unit->identity->scope;
        });
    assert(consumer_instance != design.systemverilog_hir.instances().end());
    assert(consumer_instance->id.valid());
    assert(consumer_instance->source == consumer_unit->identity->source);
    assert(consumer_instance->origin
        == design.semantics.instances()
               .at(consumer_instance->id.value())
               .origin);
    const auto instance_view = design.find_instance(consumer_instance->id);
    assert(instance_view && *instance_view);
    assert(instance_view->systemverilog == &*consumer_instance);

    const auto consumer_declaration = std::ranges::find_if(
        design.systemverilog_hir.declarations(),
        [&](const auto& declaration) {
            return declaration.scope == consumer_unit->identity->scope;
        });
    assert(consumer_declaration
        != design.systemverilog_hir.declarations().end());
    const auto declaration_view = design.find_declaration(
        consumer_declaration->id);
    assert(declaration_view && *declaration_view);
    assert(declaration_view->systemverilog == &*consumer_declaration);
    assert(consumer_declaration->declared_type);
    const auto type_view = design.find_type(
        *consumer_declaration->declared_type);
    assert(type_view && *type_view);
    assert(type_view->systemverilog != nullptr);
    const auto derived_class = std::ranges::find(
        design.systemverilog_hir.classes(),
        std::string { "work::ConsumerPkg::Derived" },
        &sv::ClassDeclaration::canonical_identity);
    assert(derived_class != design.systemverilog_hir.classes().end());
    assert(derived_class->base);
    const auto relocated_class_default = std::ranges::find(
        design.systemverilog_hir.expressions(), std::string { "17" },
        &sv::Expression::text);
    assert(relocated_class_default
        != design.systemverilog_hir.expressions().end());
    const auto relocated_parameter = std::ranges::find(
        design.systemverilog_hir.declarations(),
        derived_class->parameters.front().declaration,
        &sv::Declaration::id);
    assert(relocated_parameter
        != design.systemverilog_hir.declarations().end());
    assert(relocated_parameter->id.valid());
    assert(relocated_parameter->scope == derived_class->scope);
    assert(derived_class->base->actuals.front().expression
        == relocated_class_default->id);
    assert(derived_class->parameters.front().declaration
        == relocated_parameter->id);
    assert(derived_class->parameters.front().default_value
        == relocated_class_default->id);
    assert(derived_class->properties.front().initializer
        == relocated_class_default->id);

    const auto configuration = design.find_unit(
        UnitKind::systemverilog_configuration, "work", "SvConfig");
    assert(configuration && configuration->systemverilog);
    assert(configuration->systemverilog->configuration);
    const auto& configuration_declaration
        = *configuration->systemverilog->configuration;
    assert(configuration_declaration.designs.size() == 1);
    assert(configuration_declaration.designs.front().target);
    assert(design.units()
               [configuration_declaration.designs.front().target->value()]
                   .name
        == "Leaf");
    assert(configuration_declaration.rules.size() == 1);
    assert(configuration_declaration.rules.front().target
        == configuration_declaration.designs.front().target);

    const auto vhdl_instance = std::ranges::find(
        design.vhdl_hir.instances(), std::string { "entity_child" },
        &vhdl::Instance::name);
    assert(vhdl_instance != design.vhdl_hir.instances().end());
    const auto vhdl_instance_view = design.find_instance(vhdl_instance->id);
    assert(vhdl_instance_view && *vhdl_instance_view);
    assert(vhdl_instance_view->systemverilog == nullptr);
    assert(vhdl_instance_view->vhdl == &*vhdl_instance);

    auto repeated = link_pair(make_link_provider(), make_link_consumer());
    assert(repeated.ok());
    assert_same_link_product(design, *repeated.design);
    auto reversed = link_pair(make_link_consumer(), make_link_provider());
    assert(reversed.ok());
    assert_same_link_product(design, *reversed.design);

    auto declaration_pair = link_compiled_designs(
        { make_prototype_definition_bundle() });
    assert(declaration_pair.ok());
    const auto selected_definition = declaration_pair.design->find_unit(
        Language::system_verilog, "work", "PrototypeDefinition");
    assert(selected_definition && selected_definition->systemverilog);
    assert(!selected_definition->systemverilog->external);
    const auto declaration_candidates = declaration_pair.design->find_units(
        UnitKind::verilog_module, "work", "PrototypeDefinition");
    assert(declaration_candidates.size() == 2);
    assert(std::ranges::count_if(
               declaration_candidates,
               [](const CompiledUnitView candidate) {
                   return candidate.systemverilog != nullptr
                       && candidate.systemverilog->external;
               })
        == 1);

    auto external_declaration = make_collision_bundle(
        "ExternalDefinition", {}, {});
    external_declaration.mutable_systemverilog()
        .mutable_units()
        .front()
        .external = true;
    auto separately_compiled_definition = link_pair(
        std::move(external_declaration),
        make_collision_bundle("ExternalDefinition", {}, {}));
    assert(separately_compiled_definition.ok());
    const auto linked_definition
        = separately_compiled_definition.design->find_unit(
            Language::system_verilog, "work", "ExternalDefinition");
    assert(linked_definition && linked_definition->systemverilog);
    assert(!linked_definition->systemverilog->external);

    LinkBundleBuilder external_consumer_builder { "external-consumer.sv" };
    const auto external_consumer
        = external_consumer_builder.add_systemverilog_unit(
            UnitKind::verilog_module, sv::UnitKind::module,
            "ExternalConsumer");
    external_consumer_builder.add_systemverilog_graph(
        external_consumer, "ExternalDefinition", "7");
    auto external_reference = make_collision_bundle(
        "ExternalDefinition", {}, {});
    external_reference.mutable_systemverilog()
        .mutable_units()
        .front()
        .external = true;
    std::vector<CompiledDesign> external_reference_inputs;
    external_reference_inputs.push_back(std::move(external_reference));
    external_reference_inputs.push_back(
        make_collision_bundle("ExternalDefinition", {}, {}));
    external_reference_inputs.push_back(
        external_consumer_builder.finish());
    const auto linked_external_reference
        = link_compiled_designs(std::move(external_reference_inputs));
    assert(linked_external_reference.ok());
    assert(has_reference(*linked_external_reference.design,
        CompiledReferenceKind::module, "ExternalConsumer",
        UnitKind::verilog_module, "ExternalDefinition"));

    auto duplicate_unit = link_pair(
        make_collision_bundle("SameUnit", {}, {}),
        make_collision_bundle("SameUnit", {}, {}));
    assert(!duplicate_unit.ok());
    assert(duplicate_unit.error.find("unit collision")
        != std::string::npos);

    auto cross_language_module = link_pair(
        make_collision_bundle(
            "SharedModule", {}, {}, Language::verilog),
        make_collision_bundle(
            "SharedModule", {}, {}, Language::system_verilog));
    assert(!cross_language_module.ok());
    assert(cross_language_module.error.find("unit collision")
        != std::string::npos);

    auto vhdl_case_collision = link_pair(
        make_vhdl_collision_bundle("CaseEntity", "WORK"),
        make_vhdl_collision_bundle("caseentity", "work"));
    assert(!vhdl_case_collision.ok());
    assert(vhdl_case_collision.error.find("unit collision")
        != std::string::npos);

    auto duplicate_class = link_pair(
        make_collision_bundle(
            "ClassOwnerA", "work::$unit::Duplicate", {}),
        make_collision_bundle(
            "ClassOwnerB", "work::$unit::Duplicate", {}));
    assert(!duplicate_class.ok());
    assert(duplicate_class.error.find("class collision")
        != std::string::npos);

    auto duplicate_udp = link_pair(
        make_collision_bundle("UdpOwnerA", {}, "duplicate_udp"),
        make_collision_bundle("UdpOwnerB", {}, "duplicate_udp"));
    assert(!duplicate_udp.ok());
    assert(duplicate_udp.error.find("UDP collision")
        != std::string::npos);

    auto dangling_expression = make_link_consumer();
    dangling_expression.mutable_systemverilog()
        .mutable_expressions()
        .front()
        .operands.push_back(ExpressionId::from_index(1000));
    std::vector<CompiledDesign> dangling_expression_inputs;
    dangling_expression_inputs.push_back(std::move(dangling_expression));
    const auto rejected_expression = link_compiled_designs(
        std::move(dangling_expression_inputs));
    assert(!rejected_expression.ok());
    assert(rejected_expression.error.find("structurally invalid")
        != std::string::npos);

    auto dangling_class_member = make_link_consumer();
    dangling_class_member.mutable_systemverilog()
        .mutable_classes()
        .front()
        .parameters.front()
        .declaration = DeclarationId::from_index(1000);
    std::vector<CompiledDesign> dangling_class_inputs;
    dangling_class_inputs.push_back(std::move(dangling_class_member));
    const auto rejected_class = link_compiled_designs(
        std::move(dangling_class_inputs));
    assert(!rejected_class.ok());
    assert(rejected_class.error.find("SystemVerilog class HIR")
        != std::string::npos);

    auto dangling_source = make_collision_bundle(
        "DanglingSource", "work::$unit::Dangling", {});
    dangling_source.mutable_systemverilog()
        .mutable_classes()
        .front()
        .source = SourceSpanId::from_index(1000);
    std::vector<CompiledDesign> dangling_source_inputs;
    dangling_source_inputs.push_back(std::move(dangling_source));
    assert(!link_compiled_designs(
                std::move(dangling_source_inputs))
                .ok());

    auto duplicate_record = make_link_consumer();
    duplicate_record.mutable_systemverilog()
        .mutable_expressions()
        .push_back(duplicate_record.systemverilog_hir.expressions().front());
    std::vector<CompiledDesign> duplicate_record_inputs;
    duplicate_record_inputs.push_back(std::move(duplicate_record));
    assert(!link_compiled_designs(
                std::move(duplicate_record_inputs))
                .ok());

    auto missing_process = make_link_consumer();
    auto& missing_process_hir = missing_process.mutable_systemverilog();
    const auto missing_process_id = missing_process_hir.processes().front().id;
    std::erase_if(missing_process_hir.mutable_processes(),
        [&](const sv::Process& process) {
            return process.id == missing_process_id;
        });
    for (auto& unit : missing_process_hir.mutable_units()) {
        std::erase(unit.processes, missing_process_id);
    }
    std::vector<CompiledDesign> missing_process_inputs;
    missing_process_inputs.push_back(std::move(missing_process));
    const auto rejected_missing_process = link_compiled_designs(
        std::move(missing_process_inputs));
    assert(!rejected_missing_process.ok());
    assert(rejected_missing_process.error.find(
               "semantic process identity")
        != std::string::npos);
    assert(rejected_missing_process.error.find(
               "has no SystemVerilog or VHDL HIR record")
        != std::string::npos);
}

void test_compiled_design_relocation_regressions()
{
    auto specialization = make_specialization_fixture();
    const auto relocated = link_pair(
        make_relocation_anchor_bundle(), std::move(specialization.design));
    assert(relocated.ok());
    assert(relocated.design && relocated.design->valid());

    const auto& systemverilog_nested = semantic_instance(
        *relocated.design, "nested_child", Language::system_verilog);
    assert(systemverilog_nested.id.value() != 0);
    assert(qualified_instance_identity(
               *relocated.design, systemverilog_nested.id)
        == "SpecializedSv.nested_generated.nested_child");
    const auto& vhdl_generated = semantic_instance(
        *relocated.design, "generated_child", Language::vhdl);
    assert(vhdl_generated.id.value() != 0);
    assert(qualified_instance_identity(
               *relocated.design, vhdl_generated.id)
        == "Entity.generated.generated_child");
    const auto& vhdl_nested = semantic_instance(
        *relocated.design, "nested_child", Language::vhdl);
    assert(qualified_instance_identity(*relocated.design, vhdl_nested.id)
        == "Entity.generated.nested_generated.nested_child");

    auto reverse_specialization = make_specialization_fixture();
    const auto reverse_relocated = link_pair(
        std::move(reverse_specialization.design),
        make_relocation_anchor_bundle());
    assert(reverse_relocated.ok());
    assert_same_link_product(
        *relocated.design, *reverse_relocated.design);

    std::vector<CompiledDesign> library_inputs;
    library_inputs.push_back(make_library_target_bundle("fallback_lib"));
    library_inputs.push_back(make_library_consumer_bundle());
    library_inputs.push_back(make_library_target_bundle("selected_lib"));
    const auto libraries = link_compiled_designs(std::move(library_inputs));
    assert(libraries.ok());
    const auto selected_reference = std::ranges::find_if(
        libraries.design->references(), [&](const auto& reference) {
            return reference.kind == CompiledReferenceKind::module
                && reference.library == "selected_lib"
                && reference.name == "SharedTarget"
                && reference.owner.valid()
                && libraries.design->units()[reference.owner.value()].name
                    == "LibraryConsumer";
        });
    assert(selected_reference != libraries.design->references().end());
    assert(selected_reference->target);
    const auto& selected_target
        = referenced_unit(*libraries.design, *selected_reference);
    assert(selected_target.library == "selected_lib");
    const auto fallback_target = libraries.design->find_unit(
        Language::system_verilog, "fallback_lib", "SharedTarget");
    assert(fallback_target);
    assert(fallback_target->identity->id != selected_target.id);

    std::vector<CompiledDesign> reverse_library_inputs;
    reverse_library_inputs.push_back(
        make_library_target_bundle("selected_lib"));
    reverse_library_inputs.push_back(make_library_consumer_bundle());
    reverse_library_inputs.push_back(
        make_library_target_bundle("fallback_lib"));
    const auto reverse_libraries
        = link_compiled_designs(std::move(reverse_library_inputs));
    assert(reverse_libraries.ok());
    assert_same_link_product(*libraries.design, *reverse_libraries.design);

    auto duplicate_generated_owner = make_specialization_fixture();
    auto& duplicate_unit = duplicate_generated_owner.design
                               .mutable_systemverilog()
                               .mutable_units()
                               .front();
    assert(!duplicate_unit.generates.empty());
    auto& duplicate_generate = duplicate_unit.generates.front();
    assert(!duplicate_generate.nested.empty());
    assert(!duplicate_generate.nested.front().instances.empty());
    duplicate_generate.instances.push_back(
        duplicate_generate.nested.front().instances.front());
    std::vector<CompiledDesign> duplicate_owner_inputs;
    duplicate_owner_inputs.push_back(
        std::move(duplicate_generated_owner.design));
    const auto rejected_duplicate_owner
        = link_compiled_designs(std::move(duplicate_owner_inputs));
    assert(!rejected_duplicate_owner.ok());
    assert(rejected_duplicate_owner.error.find("structurally invalid")
        != std::string::npos);

    auto corrupt_origin = make_link_consumer();
    corrupt_origin.mutable_systemverilog()
        .mutable_instances()
        .front()
        .origin = OriginId::from_index(1000);
    std::vector<CompiledDesign> corrupt_origin_inputs;
    corrupt_origin_inputs.push_back(std::move(corrupt_origin));
    const auto rejected_origin
        = link_compiled_designs(std::move(corrupt_origin_inputs));
    assert(!rejected_origin.ok());
    assert(rejected_origin.error.find("SystemVerilog instance HIR")
        != std::string::npos);
}

void test_systemverilog_auxiliary_record_relocation()
{
    auto auxiliary = make_systemverilog_auxiliary_record_bundle();
    const std::array invalid_source_token_ids {
        SourceSpanId { },
        SourceSpanId::from_index(1000),
    };
    for (const auto source : invalid_source_token_ids) {
        auto invalid_source_token = auxiliary;
        invalid_source_token.mutable_systemverilog()
            .mutable_dpi_declarations()
            .front()
            .return_type_tokens.front()
            .source = source;
        std::vector<CompiledDesign> invalid_source_token_inputs;
        invalid_source_token_inputs.push_back(
            std::move(invalid_source_token));
        const auto rejected_source_token = link_compiled_designs(
            std::move(invalid_source_token_inputs));
        assert(!rejected_source_token.ok());
        assert(rejected_source_token.error.find("SystemVerilog DPI HIR")
            != std::string::npos);
    }
    const auto original_dpi_owner_scope
        = auxiliary.systemverilog_hir.dpi_declarations().front().owner_scope;
    const auto original_dpi_source
        = auxiliary.systemverilog_hir.dpi_declarations().front().source;
    const auto original_dpi_origin
        = auxiliary.systemverilog_hir.dpi_declarations().front().origin;
    const auto original_constructor_actual
        = auxiliary.systemverilog_hir.covergroup_instances()
              .front()
              .constructor_actuals.front();

    LinkBundleBuilder anchor_builder { "aa-auxiliary-anchor.sv" };
    const auto anchor = anchor_builder.add_systemverilog_unit(
        UnitKind::verilog_module, sv::UnitKind::module,
        "AAuxiliaryAnchor");
    anchor_builder.add_systemverilog_graph(
        anchor, "AAuxiliaryAnchor", "1");

    std::vector<CompiledDesign> inputs;
    inputs.push_back(std::move(auxiliary));
    inputs.push_back(anchor_builder.finish());
    const auto linked = link_compiled_designs(std::move(inputs));
    assert(linked.ok());
    assert(linked.design && linked.design->valid());

    const auto anchor_unit = linked.design->find_unit(
        Language::system_verilog, "work", "AAuxiliaryAnchor");
    const auto auxiliary_unit = linked.design->find_unit(
        Language::system_verilog, "work", "ZZAuxiliaryRecords");
    assert(anchor_unit && auxiliary_unit);
    assert(anchor_unit->identity->id.value() == 0);
    assert(auxiliary_unit->identity->id.value() != 0);

    const auto& dpi_declarations
        = linked.design->systemverilog_hir.dpi_declarations();
    assert(dpi_declarations.size() == 1);
    const auto& dpi = dpi_declarations.front();
    assert(dpi.systemverilog_name == "ZZAuxiliaryRecords_dpi");
    assert(dpi.linkage_name == "ZZAuxiliaryRecords_dpi_linkage");
    assert(dpi.owner_scope == auxiliary_unit->identity->scope);
    assert(dpi.owner_scope != original_dpi_owner_scope);
    assert(dpi.source != original_dpi_source);
    assert(dpi.profile_source == dpi.source);
    assert(dpi.origin != original_dpi_origin);
    assert(dpi.source.value()
        < linked.design->semantics.source_spans().size());
    assert(dpi.origin.value()
        < linked.design->semantics.origins().size());
    assert(dpi.return_type_tokens.size() == 1);
    assert(dpi.return_type_tokens.front().source == dpi.source);
    const auto& dpi_span
        = linked.design->semantics.source_spans()[dpi.source.value()];
    assert(dpi_span.logical_name == "zz-auxiliary-records.svh");

    const auto& covergroup_instances
        = linked.design->systemverilog_hir.covergroup_instances();
    assert(covergroup_instances.size() == 1);
    const auto& covergroup = covergroup_instances.front();
    assert(covergroup.runtime_identity
        == "work::ZZAuxiliaryRecords::coverage#default");
    assert(covergroup.owner_scope == dpi.owner_scope);
    assert(covergroup.source == dpi.source);
    assert(covergroup.origin == dpi.origin);
    assert(covergroup.constructor_actuals.size() == 1);
    assert(covergroup.constructor_actuals.front()
        != original_constructor_actual);
    const auto relocated_actual = std::ranges::find(
        linked.design->systemverilog_hir.expressions(),
        covergroup.constructor_actuals.front(), &sv::Expression::id);
    assert(relocated_actual
        != linked.design->systemverilog_hir.expressions().end());
    assert(relocated_actual->text == "41");
    assert(relocated_actual->source != covergroup.source);
    const auto& actual_span = linked.design->semantics.source_spans().at(
        relocated_actual->source.value());
    assert(actual_span.logical_name == "zz-auxiliary-records.sv");
}

void test_systemverilog_auxiliary_record_projection()
{
    std::vector<CompiledDesign> inputs;
    inputs.push_back(make_systemverilog_auxiliary_record_bundle(
        "work-auxiliary", "WorkAuxiliary", "work"));
    inputs.push_back(make_systemverilog_auxiliary_record_bundle(
        "selected-auxiliary", "SelectedAuxiliary", "selected"));
    const auto linked = link_compiled_designs(std::move(inputs));
    assert(linked.ok());
    assert(linked.design && linked.design->valid());
    assert(linked.design->systemverilog_hir.dpi_declarations().size()
        == 2);
    assert(linked.design->systemverilog_hir.covergroup_instances().size()
        == 2);

    const auto extracted = extract_compiled_library(
        *linked.design, "selected");
    assert(extracted.ok());
    assert(extracted.design && extracted.design->valid());
    const auto owner = extracted.design->find_unit(
        Language::system_verilog, "selected", "SelectedAuxiliary");
    assert(owner);
    assert(!extracted.design->find_unit(
        Language::system_verilog, "work", "WorkAuxiliary"));

    const auto& declarations
        = extracted.design->systemverilog_hir.dpi_declarations();
    assert(declarations.size() == 1);
    assert(declarations.front().owner_scope == owner->identity->scope);
    assert(declarations.front().owner_identity
        == "selected::SelectedAuxiliary");
    const auto& dpi_span = extracted.design->semantics.source_spans().at(
        declarations.front().source.value());
    assert(dpi_span.logical_name == "selected-auxiliary.svh");
    assert(declarations.front().return_type_tokens.size() == 1);
    const auto& token_span = extracted.design->semantics.source_spans().at(
        declarations.front().return_type_tokens.front().source.value());
    assert(token_span.logical_name == "selected-auxiliary.svh");
    assert(std::ranges::none_of(
        extracted.design->semantics.source_spans(),
        [](const SourceSpan& source) {
            return source.logical_name == "work-auxiliary.svh";
        }));

    const auto& covergroups
        = extracted.design->systemverilog_hir.covergroup_instances();
    assert(covergroups.size() == 1);
    assert(covergroups.front().owner_scope == owner->identity->scope);
    assert(covergroups.front().owner_identity
        == "selected::SelectedAuxiliary");
    assert(covergroups.front().runtime_identity
        == "selected::SelectedAuxiliary::coverage#default");

    auto invalid_dpi_scope = *extracted.design;
    invalid_dpi_scope.mutable_systemverilog()
        .mutable_dpi_declarations()
        .front()
        .owner_scope = ScopeId::from_index(1000);
    assert(!invalid_dpi_scope.valid());

    auto foreign_dpi_scope = *extracted.design;
    const auto& retained_unit
        = foreign_dpi_scope.semantics.units().front();
    const auto foreign_owner = foreign_dpi_scope.mutable_semantics().add_unit(
        Language::vhdl, UnitKind::vhdl_package, "selected",
        "ForeignOwner", {}, retained_unit.source, retained_unit.origin);
    const auto& foreign_identity
        = foreign_dpi_scope.semantics.units().at(foreign_owner.value());
    vhdl::Unit foreign_record;
    foreign_record.id = foreign_owner;
    foreign_record.scope = foreign_identity.scope;
    foreign_record.kind = vhdl::UnitKind::package;
    foreign_record.library = foreign_identity.library;
    foreign_record.name = foreign_identity.name;
    foreign_record.source = foreign_identity.source;
    foreign_record.origin = foreign_identity.origin;
    foreign_dpi_scope.mutable_vhdl().mutable_units().push_back(
        std::move(foreign_record));
    foreign_dpi_scope.mutable_systemverilog()
        .mutable_dpi_declarations()
        .front()
        .owner_scope = foreign_identity.scope;
    assert(!foreign_dpi_scope.valid());

    auto mismatched_dpi_owner_kind = *extracted.design;
    mismatched_dpi_owner_kind.mutable_systemverilog()
        .mutable_dpi_declarations()
        .front()
        .owner_kind = sv::DpiOwnerKind::compilation_unit;
    assert(!mismatched_dpi_owner_kind.valid());

    auto invalid_covergroup_scope = *extracted.design;
    invalid_covergroup_scope.mutable_systemverilog()
        .mutable_covergroup_instances()
        .front()
        .owner_scope = ScopeId { };
    assert(!invalid_covergroup_scope.valid());
}

void test_compiled_systemverilog_resolver_exports_and_lets()
{
    LinkBundleBuilder builder { "resolver-systemverilog-exports.sv" };
    const auto source_package = builder.add_systemverilog_unit(
        UnitKind::systemverilog_package, sv::UnitKind::package,
        "source_values");
    const auto export_package = builder.add_systemverilog_unit(
        UnitKind::systemverilog_package, sv::UnitKind::package,
        "exported_values");
    const auto consumer = builder.add_systemverilog_unit(
        UnitKind::verilog_module, sv::UnitKind::module,
        "consumer");
    const auto source_scope
        = builder.model.units()[source_package.value()].scope;
    const auto consumer_scope
        = builder.model.units()[consumer.value()].scope;

    const auto value = builder.model.add_declaration(source_scope,
        DeclarationKind::constant, "VALUE", builder.source,
        builder.origin);
    sv::Declaration value_record;
    value_record.id = value;
    value_record.scope = source_scope;
    value_record.form = sv::DeclarationForm::parameter;
    value_record.name = "VALUE";
    value_record.source = builder.source;
    value_record.origin = builder.origin;
    builder.systemverilog.mutable_declarations().push_back(
        std::move(value_record));
    builder.systemverilog_unit(source_package)
        .declarations.push_back(value);

    const auto let_expression = builder.model.add_expression_identity(
        source_scope, builder.source, builder.origin);
    sv::Expression expression;
    expression.id = let_expression;
    expression.scope = source_scope;
    expression.kind = sv::ExpressionKind::integer_literal;
    expression.text = "1";
    expression.source = builder.source;
    expression.origin = builder.origin;
    builder.systemverilog.mutable_expressions().push_back(
        std::move(expression));
    sv::LetDeclaration let;
    let.name = "increment";
    let.expression = let_expression;
    let.source = builder.source;
    let.origin = builder.origin;
    builder.systemverilog_unit(source_package).lets.push_back(
        std::move(let));

    sv::Import imported;
    imported.package.spelling = "source_values";
    imported.package.source = builder.source;
    imported.wildcard = true;
    imported.source = builder.source;
    builder.systemverilog_unit(export_package).imports.push_back(imported);
    sv::Export exported;
    exported.package.spelling = "source_values";
    exported.package.source = builder.source;
    exported.wildcard = true;
    exported.source = builder.source;
    builder.systemverilog_unit(export_package).exports.push_back(exported);

    sv::Import consumer_import;
    consumer_import.package.spelling = "exported_values";
    consumer_import.package.source = builder.source;
    consumer_import.wildcard = true;
    consumer_import.source = builder.source;
    builder.systemverilog_unit(consumer).imports.push_back(
        std::move(consumer_import));

    auto design = builder.finish();
    assert(design.valid());
    const CompiledDesignResolver resolver { design, consumer };
    assert(resolver.resolve_systemverilog_constant(
                       "VALUE", consumer_scope, false)
               .unique()
        == value);
    assert(resolver.resolve_systemverilog_constant(
                       "exported_values::VALUE", consumer_scope, false)
               .unique()
        == value);
    const auto resolved_let = resolver.resolve_systemverilog_let(
        "increment", consumer_scope);
    assert(resolved_let.status == CompiledResolutionStatus::unique);
    assert(resolved_let.unique() != nullptr);
    assert(resolved_let.unique()->expression == let_expression);
    const auto qualified_let = resolver.resolve_systemverilog_let(
        "exported_values::increment", consumer_scope);
    assert(qualified_let.status == CompiledResolutionStatus::unique);
    assert(qualified_let.unique() == resolved_let.unique());
}

void test_compiled_systemverilog_resolver_enum_literal_duplicates()
{
    LinkBundleBuilder builder { "resolver-systemverilog-enum-literals.sv" };
    const auto enum_package = builder.add_systemverilog_unit(
        UnitKind::systemverilog_package, sv::UnitKind::package,
        "enum_values");
    const auto parameter_package = builder.add_systemverilog_unit(
        UnitKind::systemverilog_package, sv::UnitKind::package,
        "parameter_values");
    const auto consumer = builder.add_systemverilog_unit(
        UnitKind::verilog_module, sv::UnitKind::module, "consumer");

    const auto add_declaration = [&](const UnitId unit,
                                     const DeclarationKind kind,
                                     const sv::DeclarationForm form,
                                     const std::string& name,
                                     const std::optional<OriginId> origin
                                         = std::nullopt) {
        const auto scope = builder.model.units()[unit.value()].scope;
        const auto declaration_origin = origin.value_or(builder.origin);
        const auto id = builder.model.add_declaration(
            scope, kind, name, builder.source, declaration_origin);
        sv::Declaration declaration;
        declaration.id = id;
        declaration.scope = scope;
        declaration.form = form;
        declaration.name = name;
        declaration.source = builder.source;
        declaration.origin = declaration_origin;
        builder.systemverilog.mutable_declarations().push_back(
            std::move(declaration));
        builder.systemverilog_unit(unit).declarations.push_back(id);
        return id;
    };

    const auto enum_literal = add_declaration(
        consumer, DeclarationKind::enumeration_literal,
        sv::DeclarationForm::enumeration_literal, "READY");
    const auto synthetic_origin = builder.model.add_origin(
        OriginKind::parsed, builder.source, builder.origin,
        "synthetic enum local parameter");
    const auto synthetic_parameter = add_declaration(
        consumer, DeclarationKind::generic,
        sv::DeclarationForm::local_parameter, "READY", synthetic_origin);
    const auto imported_enum = add_declaration(
        enum_package, DeclarationKind::enumeration_literal,
        sv::DeclarationForm::enumeration_literal, "CONFLICT");
    const auto imported_parameter = add_declaration(
        parameter_package, DeclarationKind::generic,
        sv::DeclarationForm::parameter, "CONFLICT");

    for (const auto package : { "enum_values", "parameter_values" }) {
        sv::Import import;
        import.package.spelling = package;
        import.package.source = builder.source;
        import.wildcard = true;
        import.source = builder.source;
        builder.systemverilog_unit(consumer).imports.push_back(
            std::move(import));
    }

    auto design = builder.finish();
    assert(design.valid());
    const auto scope = design.units()[consumer.value()].scope;
    const CompiledDesignResolver resolver { design, consumer };

    sv::Name retained;
    retained.spelling = "READY";
    retained.source = builder.source;
    retained.overloads = { enum_literal, synthetic_parameter };
    const auto retained_result = resolver.resolve_systemverilog_name(
        retained, scope);
    assert(retained_result.status == CompiledResolutionStatus::unique);
    assert(retained_result.unique() == enum_literal);

    sv::Name lexical;
    lexical.spelling = "READY";
    lexical.source = builder.source;
    const auto lexical_result = resolver.resolve_systemverilog_name(
        lexical, scope);
    assert(lexical_result.status == CompiledResolutionStatus::unique);
    assert(lexical_result.unique() == enum_literal);

    sv::Name imported;
    imported.spelling = "CONFLICT";
    imported.source = builder.source;
    const auto imported_result = resolver.resolve_systemverilog_name(
        imported, scope);
    assert(imported_result.status == CompiledResolutionStatus::ambiguous);
    assert(std::ranges::find(
               imported_result.candidates, imported_enum)
        != imported_result.candidates.end());
    assert(std::ranges::find(
               imported_result.candidates, imported_parameter)
        != imported_result.candidates.end());
}

void test_compiled_systemverilog_resolver_expression_targets()
{
    LinkBundleBuilder builder { "resolver-systemverilog-targets.sv" };
    const auto unit = builder.add_systemverilog_unit(
        UnitKind::verilog_module, sv::UnitKind::module, "consumer");
    const auto scope = builder.model.units()[unit.value()].scope;

    const auto type_declaration = builder.model.add_declaration(
        scope, DeclarationKind::type, "payload_t",
        builder.source, builder.origin);
    TypeReference model_base;
    model_base.source = builder.source;
    model_base.spelling = "logic";
    const auto type = builder.model.add_type(
        scope, TypeKind::declaration, "payload_t", model_base,
        builder.source, builder.origin);
    sv::TypeDefinition definition;
    definition.id = type;
    definition.declaration = type_declaration;
    definition.form = sv::TypeForm::packed_structure;
    definition.name = "payload_t";
    definition.base.target.source = builder.source;
    definition.base.target.spelling = "logic";
    definition.source = builder.source;
    definition.origin = builder.origin;
    sv::PackedMember field;
    field.name = "field";
    field.type.target.source = builder.source;
    field.type.target.spelling = "logic";
    field.type.value_form = sv::TypeForm::packed_integral;
    field.type.executable_width = 1U;
    field.source = builder.source;
    definition.members.push_back(std::move(field));
    builder.systemverilog.mutable_types().push_back(
        std::move(definition));

    sv::Declaration type_record;
    type_record.id = type_declaration;
    type_record.scope = scope;
    type_record.form = sv::DeclarationForm::typedef_declaration;
    type_record.name = "payload_t";
    type_record.source = builder.source;
    type_record.origin = builder.origin;
    type_record.declared_type = type;
    type_record.type.emplace();
    type_record.type->target.source = builder.source;
    type_record.type->target.spelling = "logic";
    type_record.type->value_form = sv::TypeForm::packed_integral;
    type_record.type->executable_width = 1U;
    builder.systemverilog.mutable_declarations().push_back(
        std::move(type_record));

    const auto payload = builder.model.add_declaration(
        scope, DeclarationKind::variable, "payload",
        builder.source, builder.origin);
    sv::Declaration payload_record;
    payload_record.id = payload;
    payload_record.scope = scope;
    payload_record.form = sv::DeclarationForm::variable;
    payload_record.name = "payload";
    payload_record.source = builder.source;
    payload_record.origin = builder.origin;
    payload_record.type.emplace();
    payload_record.type->target.target = type;
    payload_record.type->target.source = builder.source;
    payload_record.type->target.spelling = "payload_t";
    builder.systemverilog.mutable_declarations().push_back(
        std::move(payload_record));
    auto& declarations = builder.systemverilog_unit(unit).declarations;
    declarations.push_back(type_declaration);
    declarations.push_back(payload);

    const auto add_name = [&](std::string spelling) {
        const auto id = builder.model.add_expression_identity(
            scope, builder.source, builder.origin);
        sv::Expression expression;
        expression.id = id;
        expression.scope = scope;
        expression.kind = sv::ExpressionKind::name;
        expression.text = std::move(spelling);
        expression.source = builder.source;
        expression.origin = builder.origin;
        builder.systemverilog.mutable_expressions().push_back(
            std::move(expression));
        return id;
    };
    const auto member = add_name("payload.field");
    const auto missing_member = add_name("payload.missing");
    const auto subscript = builder.model.add_expression_identity(
        scope, builder.source, builder.origin);
    sv::Expression subscript_record;
    subscript_record.id = subscript;
    subscript_record.scope = scope;
    subscript_record.kind = sv::ExpressionKind::integer_literal;
    subscript_record.text = "0";
    subscript_record.source = builder.source;
    subscript_record.origin = builder.origin;
    builder.systemverilog.mutable_expressions().push_back(
        std::move(subscript_record));
    const auto index = builder.model.add_expression_identity(
        scope, builder.source, builder.origin);
    sv::Expression index_record;
    index_record.id = index;
    index_record.scope = scope;
    index_record.kind = sv::ExpressionKind::index;
    index_record.text = "[]";
    index_record.operands.push_back(member);
    index_record.operands.push_back(subscript);
    index_record.source = builder.source;
    index_record.origin = builder.origin;
    builder.systemverilog.mutable_expressions().push_back(
        std::move(index_record));

    const auto design = builder.finish();
    assert(design.valid());
    const CompiledDesignResolver resolver { design, unit };
    sv::Name retained;
    retained.spelling = "payload";
    retained.selected = payload;
    retained.overloads.push_back(payload);
    assert(resolver.resolve_systemverilog_name(retained, scope).unique()
        == payload);
    retained.spelling = "missing";
    assert(resolver.resolve_systemverilog_name(retained, scope).status
        == CompiledResolutionStatus::not_found);
    assert(resolver.resolve_systemverilog_expression(member).unique()
        == payload);
    assert(resolver.resolve_systemverilog_expression(missing_member).status
        == CompiledResolutionStatus::not_found);
    assert(resolver.resolve_systemverilog_target(index).unique()
        == payload);
}

void test_compiled_systemverilog_resolver_synthetic_type_designator()
{
    LinkBundleBuilder builder { "resolver-synthetic-type.sv" };
    const auto unit = builder.add_systemverilog_unit(
        UnitKind::verilog_module, sv::UnitKind::module, "consumer");
    const auto scope = builder.model.units()[unit.value()].scope;
    const auto declaration = builder.model.add_declaration(
        scope, DeclarationKind::type, "control_t",
        builder.source, builder.origin);

    TypeReference model_base;
    model_base.source = builder.source;
    model_base.spelling = "wire";
    const auto type = builder.model.add_type(
        scope, TypeKind::declaration, "control_t", model_base,
        builder.source, builder.origin);

    sv::TypeDefinition definition;
    definition.id = type;
    definition.declaration = declaration;
    definition.form = sv::TypeForm::unpacked_structure;
    definition.name = "control_t";
    definition.base.target.source = builder.source;
    definition.base.target.spelling = "wire";
    definition.source = builder.source;
    definition.origin = builder.origin;
    builder.systemverilog.mutable_types().push_back(
        std::move(definition));

    sv::Declaration designator;
    designator.id = declaration;
    designator.scope = scope;
    designator.form = sv::DeclarationForm::net;
    designator.name = "control_t";
    designator.source = builder.source;
    designator.origin = builder.origin;
    designator.type.emplace();
    designator.type->target.source = builder.source;
    designator.type->target.spelling = "wire";
    designator.type->executable_width = 1U;
    builder.systemverilog.mutable_declarations().push_back(
        std::move(designator));
    builder.systemverilog_unit(unit).declarations.push_back(declaration);

    const auto design = builder.finish();
    assert(design.valid());
    const CompiledDesignResolver resolver { design, unit };
    assert(resolver.resolve_systemverilog_named_type(
                       "control_t", scope, false)
               .unique()
        == declaration);

    sv::TypeReference reference;
    reference.target.target = type;
    reference.target.source = builder.source;
    reference.target.spelling = "control_t";
    const auto effective = resolver.effective_systemverilog_type(
        reference, scope);
    assert(effective);
    assert(effective->target.target == type);
    assert(effective->target.spelling == "control_t");
    assert(effective->value_form
        == sv::TypeForm::unpacked_structure);

    sv::TypeReference spelling_only;
    spelling_only.target.source = builder.source;
    spelling_only.target.spelling = "control_t";
    const auto resolved_spelling = resolver.effective_systemverilog_type(
        spelling_only, scope);
    assert(resolved_spelling);
    assert(resolved_spelling->target.target == type);
    assert(resolved_spelling->target.spelling == "control_t");
    assert(resolved_spelling->value_form
        == sv::TypeForm::unpacked_structure);
}

void test_compiled_design_specialization()
{
    const auto fixture = make_specialization_fixture();
    assert(fixture.design.valid());

    const std::vector<SpecializedHirActualIdentity> sv_actuals {
        { fixture.type_parameter, "logic[7:0]" },
        { fixture.parameter, "7" },
    };
    const auto systemverilog = make_specialized_hir_overlay(
        fixture.design, fixture.systemverilog_unit, sv_actuals);
    assert(systemverilog);
    assert(systemverilog->unit == fixture.systemverilog_unit);
    assert(systemverilog->scope
        == fixture.design.units()[fixture.systemverilog_unit.value()].scope);
    assert(systemverilog->language == Language::system_verilog);
    assert(systemverilog->actual_identities.size() == 2);
    assert(systemverilog->actual_identities.front().declaration
        == fixture.parameter);
    assert(systemverilog->actual_identities.back().declaration
        == fixture.type_parameter);
    assert((systemverilog->residual_expressions
        == std::vector<ExpressionId> {
            fixture.parameter_expression, fixture.type_expression }));
    assert((systemverilog->dependent_generates
        == std::vector<DeclarationId> {
            fixture.parameter_generate, fixture.type_generate }));
    const std::vector<SpecializedHirActualIdentity> ordered_sv_actuals {
        { fixture.parameter, "7" },
        { fixture.type_parameter, "logic[7:0]" },
    };
    const auto ordered_systemverilog = make_specialized_hir_overlay(
        fixture.design, fixture.systemverilog_unit, ordered_sv_actuals);
    assert(ordered_systemverilog == systemverilog);
    const std::vector<SpecializedHirNamedIdentity> named_sv_actuals {
        { "T", "logic[7:0]" },
        { "@specparam:delay", "32'sd2" },
        { "LOCAL", "32'sd9" },
        { "T", "ignored duplicate" },
    };
    const std::vector<SpecializedHirNamedIdentity> fallback_sv_actuals {
        { "P", "7" },
        { "T", "ignored fallback" },
        { "local_package", "work.local_package" },
    };
    const auto named_systemverilog = make_specialized_hir_overlay(
        fixture.design, fixture.systemverilog_unit,
        named_sv_actuals, fallback_sv_actuals);
    assert(named_systemverilog == systemverilog);

    const std::vector<SpecializedHirActualIdentity> vhdl_actuals {
        { fixture.architecture_generic, "13" },
        { fixture.entity_type_generic, "work.byte_t" },
        { fixture.entity_generic, "11" },
    };
    const auto vhdl = make_specialized_hir_overlay(
        fixture.design, fixture.vhdl_architecture, vhdl_actuals);
    assert(vhdl);
    assert(vhdl->unit == fixture.vhdl_architecture);
    assert(vhdl->language == Language::vhdl);
    assert((vhdl->residual_expressions
        == std::vector<ExpressionId> { fixture.entity_expression,
            fixture.entity_type_expression,
            fixture.architecture_expression }));
    assert((vhdl->dependent_generates
        == std::vector<DeclarationId> { fixture.entity_generate,
            fixture.architecture_generate }));

    const std::vector<SpecializedHirNamedIdentity> named_vhdl_actuals {
        { "gt", "work.byte_t" },
        { "gf", "work.transform(bit)returnbit" },
        { "gp", "work.update(bit)" },
        { "pkg", "work.actual_package" },
        { "@local", "ignored" },
    };
    const std::vector<SpecializedHirNamedIdentity> fallback_vhdl_actuals {
        { "g", "11" },
        { "gf", "ignored fallback" },
        { "ag", "13" },
        { "local_package", "work.local_package" },
    };
    const auto named_vhdl = make_specialized_hir_overlay(
        fixture.design, fixture.vhdl_architecture,
        named_vhdl_actuals, fallback_vhdl_actuals);
    assert(named_vhdl);
    assert(named_vhdl->actual_identities.size() == 6);
    assert((named_vhdl->residual_expressions
        == std::vector<ExpressionId> { fixture.entity_expression,
            fixture.entity_type_expression,
            fixture.entity_nonvalue_expression,
            fixture.architecture_expression }));
    const auto identity_for = [&](const DeclarationId declaration) {
        return std::ranges::find(
            named_vhdl->actual_identities, declaration,
            &SpecializedHirActualIdentity::declaration);
    };
    assert(identity_for(fixture.entity_generic)->identity == "11");
    assert(identity_for(fixture.entity_type_generic)->identity
        == "work.byte_t");
    assert(identity_for(fixture.entity_function_generic)->identity
        == "work.transform(bit)returnbit");
    assert(identity_for(fixture.entity_procedure_generic)->identity
        == "work.update(bit)");
    assert(identity_for(fixture.entity_package_generic)->identity
        == "work.actual_package");
    assert(identity_for(fixture.architecture_generic)->identity == "13");

    const std::vector<SpecializedHirActualIdentity> entity_actuals {
        { fixture.entity_generic, "17" },
    };
    const auto entity = make_specialized_hir_overlay(
        fixture.design, fixture.vhdl_entity, entity_actuals);
    assert(entity);
    assert((entity->residual_expressions
        == std::vector<ExpressionId> { fixture.entity_expression }));
    assert((entity->dependent_generates
        == std::vector<DeclarationId> { fixture.entity_generate }));

    const std::vector<SpecializedHirActualIdentity> duplicate_actuals {
        { fixture.parameter, "1" },
        { fixture.parameter, "2" },
    };
    assert(!make_specialized_hir_overlay(fixture.design,
        fixture.systemverilog_unit, duplicate_actuals));

    const std::vector<SpecializedHirActualIdentity> foreign_sv {
        { fixture.foreign_parameter, "1" },
    };
    assert(!make_specialized_hir_overlay(fixture.design,
        fixture.systemverilog_unit, foreign_sv));
    const std::vector<SpecializedHirActualIdentity> foreign_vhdl {
        { fixture.foreign_generic, "1" },
    };
    assert(!make_specialized_hir_overlay(fixture.design,
        fixture.vhdl_architecture, foreign_vhdl));
    assert(!make_specialized_hir_overlay(fixture.design,
        UnitId::from_index(1000), sv_actuals));
}

void test_compiled_design_resolver_generic_callable_profile()
{
    LinkBundleBuilder builder { "resolver-generic-profile.vhd" };
    const auto unit = builder.add_vhdl_unit(UnitKind::vhdl_entity,
        vhdl::UnitKind::entity, "ResolverProfile");
    const auto scope = builder.model.units()[unit.value()].scope;
    std::optional<TypeId> generic_type;
    const auto type_formal = add_vhdl_actual(builder, unit,
        vhdl::DeclarationForm::generic_type, "element_t", generic_type);
    assert(generic_type);

    const auto add_callable = [&](const vhdl::DeclarationForm form,
                                  const std::string& name,
                                  vhdl::SubtypeIndication subtype,
                                  const bool defined) {
        const auto declaration = builder.model.add_declaration(scope,
            form == vhdl::DeclarationForm::function
                ? DeclarationKind::function
                : DeclarationKind::generic,
            name, builder.source, builder.origin);
        const auto formal = builder.model.add_declaration(scope,
            DeclarationKind::variable, "value",
            builder.source, builder.origin);
        vhdl::Declaration formal_record;
        formal_record.id = formal;
        formal_record.scope = scope;
        formal_record.form = vhdl::DeclarationForm::variable;
        formal_record.name = "value";
        formal_record.source = builder.source;
        formal_record.origin = builder.origin;
        formal_record.direction = vhdl::Direction::input;
        formal_record.object_class = vhdl::ObjectClass::constant;
        formal_record.subtype = subtype;
        builder.vhdl_hir.mutable_declarations().push_back(
            std::move(formal_record));

        vhdl::Declaration callable;
        callable.id = declaration;
        callable.scope = scope;
        callable.form = form;
        callable.name = name;
        callable.source = builder.source;
        callable.origin = builder.origin;
        callable.subtype = subtype;
        callable.callable.emplace();
        callable.callable->function = true;
        callable.callable->pure = true;
        callable.callable->defined = defined;
        callable.callable->return_type = subtype;
        callable.callable->formals.push_back(formal);
        callable.children.push_back(formal);
        builder.vhdl_hir.mutable_declarations().push_back(
            std::move(callable));
        builder.vhdl_unit(unit).declarations.push_back(declaration);
        return declaration;
    };

    vhdl::SubtypeIndication formal_subtype;
    formal_subtype.type_mark.target = *generic_type;
    formal_subtype.type_mark.spelling = "element_t";
    const auto expected = add_callable(
        vhdl::DeclarationForm::generic_function,
        "transform", formal_subtype, false);
    vhdl::SubtypeIndication integer_subtype;
    integer_subtype.type_mark.spelling = "integer";
    integer_subtype.domain = vhdl::ValueDomain::integer;
    const auto candidate = add_callable(
        vhdl::DeclarationForm::function,
        "mapped", integer_subtype, true);
    vhdl::SubtypeIndication unspecified_subtype;
    unspecified_subtype.type_mark.spelling
        = "@vhdl-unspecified:private";
    unspecified_subtype.unspecified_class
        = vhdl::UnspecifiedTypeClass::private_type;
    vhdl::SubtypeIndication boolean_subtype;
    boolean_subtype.type_mark.spelling = "boolean";
    boolean_subtype.domain = vhdl::ValueDomain::boolean;
    const auto unspecified_declaration = add_callable(
        vhdl::DeclarationForm::function,
        "accept", unspecified_subtype, false);
    const auto concrete_body = add_callable(
        vhdl::DeclarationForm::function,
        "accept", boolean_subtype, true);
    const auto type_expression = builder.model.add_expression_identity(
        scope, builder.source, builder.origin);
    vhdl::Expression type_expression_record;
    type_expression_record.id = type_expression;
    type_expression_record.scope = scope;
    type_expression_record.kind = vhdl::ExpressionKind::name;
    type_expression_record.text = "integer";
    type_expression_record.referenced_name.emplace();
    type_expression_record.referenced_name->spelling = "integer";
    type_expression_record.referenced_name->canonical = "integer";
    type_expression_record.source = builder.source;
    type_expression_record.origin = builder.origin;
    builder.vhdl_hir.mutable_expressions().push_back(
        std::move(type_expression_record));
    auto design = builder.finish();
    const CompiledDesignResolver unresolved { design, unit };
    assert(unresolved.vhdl_callable_profile_matches(
        unspecified_declaration, concrete_body));
    assert(unresolved.vhdl_callable_profiles_homographic(
        unspecified_declaration, concrete_body));
    assert(!unresolved.vhdl_callable_profile_matches(
        expected, candidate));
    const std::array formals { type_formal };
    vhdl::Association association;
    association.expression = type_expression;
    const std::array associations { association };
    const auto frame = unresolved.bind_vhdl_generics(
        formals, associations, scope);
    assert(frame && frame->front().vhdl_type);
    assert(frame->front().vhdl_type->type_mark.spelling == "integer");
    const std::array frames { *frame };
    const CompiledDesignResolver resolved {
        design, unit, nullptr, frames };
    assert(resolved.vhdl_callable_profile_matches(
        expected, candidate));
}

void test_compiled_vhdl_resolver_retained_time_layout()
{
    LinkBundleBuilder builder { "resolver-retained-time-layout.vhd" };
    const auto unit = builder.add_vhdl_unit(UnitKind::vhdl_package,
        vhdl::UnitKind::package, "vital_timing", {}, "ieee");
    auto& package = builder.vhdl_unit(unit);
    package.standard = "2008";
    const auto scope = builder.model.units()[unit.value()].scope;

    const auto add_type = [&](const std::string& name,
                              const vhdl::TypeForm form) {
        const auto declaration = builder.model.add_declaration(scope,
            DeclarationKind::type, name, builder.source, builder.origin);
        TypeReference base;
        base.source = builder.source;
        base.spelling = name;
        const auto type = builder.model.add_type(scope,
            TypeKind::declaration, name, base,
            builder.source, builder.origin);
        vhdl::Declaration declaration_record;
        declaration_record.id = declaration;
        declaration_record.scope = scope;
        declaration_record.form = form == vhdl::TypeForm::subtype
            ? vhdl::DeclarationForm::subtype
            : vhdl::DeclarationForm::type;
        declaration_record.name = name;
        declaration_record.declared_type = type;
        declaration_record.source = builder.source;
        declaration_record.origin = builder.origin;
        builder.vhdl_hir.mutable_declarations().push_back(
            std::move(declaration_record));
        vhdl::TypeDefinition definition;
        definition.id = type;
        definition.declaration = declaration;
        definition.form = form;
        definition.name = name;
        definition.source = builder.source;
        definition.origin = builder.origin;
        builder.vhdl_hir.mutable_types().push_back(std::move(definition));
        package.declarations.push_back(declaration);
        return type;
    };
    const auto delay_type = add_type(
        "vitaldelaytype", vhdl::TypeForm::subtype);
    auto& delay_definition = builder.vhdl_hir.mutable_types().back();
    delay_definition.base.type_mark.spelling = "@builtin:time";
    delay_definition.base.domain = vhdl::ValueDomain::integer;
    delay_definition.base.executable_width = 64U;
    delay_definition.base.integer_storage_width = 64U;
    delay_definition.base.signed_value = true;

    const auto delay01z_type = add_type(
        "vitaldelaytype01z", vhdl::TypeForm::array);
    auto& delay01z_definition = builder.vhdl_hir.mutable_types().back();
    delay01z_definition.element_subtype.emplace();
    delay01z_definition.element_subtype->type_mark.target = delay_type;
    delay01z_definition.element_subtype->type_mark.spelling
        = "vitaldelaytype";
    vhdl::ArrayDimension dimension;
    dimension.index_subtype.spelling = "vitaltransitiontype";
    dimension.constraint.emplace();
    dimension.constraint->kind = vhdl::RangeKind::array_index;
    dimension.constraint->left = 0;
    dimension.constraint->right = 5;
    dimension.source = builder.source;
    delay01z_definition.array_dimensions.push_back(std::move(dimension));

    const auto period_type = add_type(
        "vitalperioddatatype", vhdl::TypeForm::record);
    auto& period_definition = builder.vhdl_hir.mutable_types().back();
    const auto scalar = [&](const std::string& spelling,
                            const vhdl::ValueDomain domain) {
        vhdl::SubtypeIndication subtype;
        subtype.type_mark.spelling = spelling;
        subtype.domain = domain;
        subtype.executable_width = 1U;
        return subtype;
    };
    vhdl::SubtypeIndication delay;
    delay.type_mark.target = delay_type;
    delay.type_mark.spelling = "vitaldelaytype";
    period_definition.record_elements = {
        { "last", scalar("std_logic", vhdl::ValueDomain::logic9),
            builder.source },
        { "rise", delay, builder.source },
        { "fall", delay, builder.source },
        { "notfirstflag",
            scalar("boolean", vhdl::ValueDomain::boolean),
            builder.source },
    };

    auto design = builder.finish();
    const CompiledDesignResolver resolver { design, unit };
    const auto effective = [&](const TypeId type,
                               const std::string& spelling) {
        vhdl::SubtypeIndication subtype;
        subtype.type_mark.target = type;
        subtype.type_mark.spelling = spelling;
        return resolver.effective_vhdl_subtype(subtype, scope);
    };
    const auto delay_effective = effective(
        delay_type, "vitaldelaytype");
    const auto delay01z_effective = effective(
        delay01z_type, "vitaldelaytype01z");
    const auto period_effective = effective(
        period_type, "vitalperioddatatype");
    assert(delay_effective
        && delay_effective->executable_width == 64U
        && delay_effective->integer_storage_width == 64U);
    assert(delay01z_effective
        && delay01z_effective->executable_width == 384U);
    assert(period_effective
        && period_effective->executable_width == 130U);
}

void test_compiled_vhdl_resolver_array_shapes()
{
    LinkBundleBuilder builder { "resolver-array-shapes.vhd" };
    const auto unit = builder.add_vhdl_unit(UnitKind::vhdl_package,
        vhdl::UnitKind::package, "array_shapes");
    const auto scope = builder.model.units()[unit.value()].scope;
    const auto declaration = builder.model.add_declaration(scope,
        DeclarationKind::type, "matrix_t",
        builder.source, builder.origin);
    TypeReference base;
    base.source = builder.source;
    base.spelling = "matrix_t";
    const auto type = builder.model.add_type(scope,
        TypeKind::declaration, "matrix_t", base,
        builder.source, builder.origin);
    vhdl::Declaration declaration_record;
    declaration_record.id = declaration;
    declaration_record.scope = scope;
    declaration_record.form = vhdl::DeclarationForm::type;
    declaration_record.name = "matrix_t";
    declaration_record.declared_type = type;
    declaration_record.source = builder.source;
    declaration_record.origin = builder.origin;
    builder.vhdl_hir.mutable_declarations().push_back(
        std::move(declaration_record));
    vhdl::TypeDefinition definition;
    definition.id = type;
    definition.declaration = declaration;
    definition.form = vhdl::TypeForm::array;
    definition.name = "matrix_t";
    definition.element_subtype.emplace();
    definition.element_subtype->type_mark.spelling = "bit";
    definition.element_subtype->domain = vhdl::ValueDomain::bit2;
    definition.element_subtype->executable_width = 1U;
    definition.array_dimensions.resize(2U);
    for (auto& dimension : definition.array_dimensions) {
        dimension.unconstrained = true;
        dimension.source = builder.source;
    }
    definition.source = builder.source;
    definition.origin = builder.origin;
    builder.vhdl_hir.mutable_types().push_back(std::move(definition));
    builder.vhdl_unit(unit).declarations.push_back(declaration);

    auto design = builder.finish();
    const CompiledDesignResolver resolver { design, unit };
    const auto constraint = [](const std::optional<std::int64_t> left,
                                const std::optional<std::int64_t> right,
                                const bool descending) {
        vhdl::RangeConstraint result;
        result.kind = vhdl::RangeKind::array_index;
        result.left = left;
        result.right = right;
        result.descending = descending;
        return result;
    };
    const auto subtype = [&](std::vector<vhdl::RangeConstraint> constraints) {
        vhdl::SubtypeIndication result;
        result.type_mark.target = type;
        result.type_mark.spelling = "matrix_t";
        result.domain = vhdl::ValueDomain::bit2;
        result.constraints = std::move(constraints);
        return result;
    };
    const auto exact = subtype({
        constraint(0, 1, false), constraint(3, 1, true) });
    const auto direction_mismatch = subtype({
        constraint(1, 0, true), constraint(3, 1, true) });
    const auto bounds_mismatch = subtype({
        constraint(1, 2, false), constraint(3, 1, true) });
    const auto rank_mismatch = subtype({ constraint(0, 5, false) });
    const auto residual = subtype({
        constraint(std::nullopt, 1, false),
        constraint(3, 1, true) });
    assert(resolver.vhdl_array_shapes_match(
        exact, scope, exact, scope).value_or(false));
    assert(!resolver.vhdl_array_shapes_match(
        exact, scope, direction_mismatch, scope).value_or(true));
    assert(!resolver.vhdl_array_shapes_match(
        exact, scope, bounds_mismatch, scope).value_or(true));
    assert(!resolver.vhdl_array_shapes_match(
        exact, scope, rank_mismatch, scope).value_or(true));
    assert(!resolver.vhdl_array_shapes_match(
        exact, scope, residual, scope).has_value());
    assert(resolver.vhdl_subtype_profiles_match(
        exact, scope, exact, scope));
    assert(!resolver.vhdl_subtype_profiles_match(
        exact, scope, direction_mismatch, scope));
    assert(!resolver.vhdl_subtype_profiles_match(
        exact, scope, bounds_mismatch, scope));
    assert(resolver.vhdl_subtype_profiles_match(
        exact, scope, residual, scope));
}

void test_compiled_association_resolution()
{
    auto fixture = make_specialization_fixture();
    auto& design = fixture.design;
    const auto add_systemverilog_port = [&] {
        auto& unit = *std::ranges::find(design.systemverilog_hir.mutable_units(),
            fixture.systemverilog_unit, &sv::Unit::id);
        const auto declaration = design.semantics.add_declaration(
            unit.scope, DeclarationKind::port, "payload",
            unit.source, unit.origin);
        sv::Declaration record;
        record.id = declaration;
        record.scope = unit.scope;
        record.form = sv::DeclarationForm::port;
        record.name = "payload";
        record.source = unit.source;
        record.origin = unit.origin;
        design.systemverilog_hir.mutable_declarations().push_back(
            std::move(record));
        unit.declarations.push_back(declaration);
        return declaration;
    };
    const auto add_vhdl_port = [&] {
        auto& entity = *std::ranges::find(design.vhdl_hir.mutable_units(),
            fixture.vhdl_entity, &vhdl::Unit::id);
        const auto declaration = design.semantics.add_declaration(
            entity.scope, DeclarationKind::port, "Payload",
            entity.source, entity.origin);
        vhdl::Declaration record;
        record.id = declaration;
        record.scope = entity.scope;
        record.form = vhdl::DeclarationForm::port;
        record.name = "Payload";
        record.source = entity.source;
        record.origin = entity.origin;
        design.vhdl_hir.mutable_declarations().push_back(
            std::move(record));
        entity.declarations.push_back(declaration);
        return declaration;
    };
    const auto systemverilog_port = add_systemverilog_port();
    const auto vhdl_port = add_vhdl_port();
    auto& architecture_generic = *std::ranges::find(
        design.vhdl_hir.mutable_declarations(),
        fixture.architecture_generic, &vhdl::Declaration::id);
    architecture_generic.initializer = fixture.architecture_expression;
    assert(design.valid());

    const std::vector<SpecializedHirActualIdentity> parent_sv_actuals {
        { fixture.parameter, "7" },
    };
    const auto parent_systemverilog = make_specialized_hir_unit(
        design, fixture.systemverilog_unit, parent_sv_actuals);
    assert(parent_systemverilog);
    sv::Instance systemverilog_instance;
    sv::ActualAssociation value_actual;
    value_actual.expression = fixture.parameter_expression;
    value_actual.source = design.systemverilog_units().front().source;
    systemverilog_instance.parameters.push_back(value_actual);
    sv::ActualAssociation type_actual;
    type_actual.formal = "T";
    type_actual.kind = sv::ActualKind::type;
    type_actual.type.emplace();
    type_actual.type->target.spelling = "logic[3:0]";
    type_actual.type->target.source = value_actual.source;
    type_actual.type->value_form = sv::TypeForm::packed_integral;
    type_actual.type->executable_width = 4U;
    type_actual.source = value_actual.source;
    systemverilog_instance.parameters.push_back(type_actual);
    sv::ActualAssociation open_port;
    open_port.formal = "payload";
    open_port.kind = sv::ActualKind::open;
    open_port.source = value_actual.source;
    systemverilog_instance.ports.push_back(open_port);
    const CompiledInstanceView systemverilog_view {
        &systemverilog_instance, nullptr
    };
    const auto systemverilog = resolve_specialized_hir_associations(
        design, fixture.systemverilog_unit, systemverilog_view,
        SpecializedHirAssociationSurface::parameters,
        &*parent_systemverilog);
    assert(systemverilog);
    assert(systemverilog.bindings.size() == 2U);
    assert(systemverilog.bindings[0].formal == fixture.parameter);
    assert(systemverilog.bindings[0].identity == "7");
    assert(systemverilog.bindings[1].formal == fixture.type_parameter);
    assert(systemverilog.bindings[1].identity.starts_with("sv-type-v1;"));
    const auto systemverilog_ports
        = resolve_specialized_hir_associations(design,
            fixture.systemverilog_unit, systemverilog_view,
            SpecializedHirAssociationSurface::ports,
            &*parent_systemverilog);
    assert(systemverilog_ports);
    assert(systemverilog_ports.bindings.size() == 1U);
    assert(systemverilog_ports.bindings.front().formal
        == systemverilog_port);
    assert(systemverilog_ports.bindings.front().kind
        == SpecializedHirAssociationKind::open);
    assert(systemverilog_ports.bindings.front().identity.empty());

    auto positional_after_named = systemverilog_instance;
    positional_after_named.parameters = {
        type_actual,
        value_actual,
    };
    assert(!resolve_specialized_hir_associations(design,
        fixture.systemverilog_unit,
        CompiledInstanceView { &positional_after_named, nullptr },
        SpecializedHirAssociationSurface::parameters,
        &*parent_systemverilog));
    auto duplicate = systemverilog_instance;
    duplicate.parameters.front().formal = "P";
    duplicate.parameters.back().formal = "P";
    assert(!resolve_specialized_hir_associations(design,
        fixture.systemverilog_unit,
        CompiledInstanceView { &duplicate, nullptr },
        SpecializedHirAssociationSurface::parameters,
        &*parent_systemverilog));

    const std::vector<SpecializedHirActualIdentity> parent_vhdl_actuals {
        { fixture.entity_generic, "11" },
    };
    const auto parent_vhdl = make_specialized_hir_unit(
        design, fixture.vhdl_architecture, parent_vhdl_actuals);
    assert(parent_vhdl);
    vhdl::Instance vhdl_instance;
    vhdl::Association generic_value;
    generic_value.expression = fixture.entity_expression;
    generic_value.source = design.vhdl_units().front().source;
    vhdl_instance.generic_map.push_back(generic_value);
    vhdl::Association generic_type;
    generic_type.formal.emplace();
    generic_type.formal->spelling = "gt";
    generic_type.kind = vhdl::AssociationKind::type;
    generic_type.type.emplace();
    generic_type.type->type_mark.spelling = "work.byte_t";
    generic_type.type->type_mark.source = generic_value.source;
    generic_type.source = generic_value.source;
    vhdl_instance.generic_map.push_back(generic_type);
    vhdl::Association generic_default;
    generic_default.formal.emplace();
    generic_default.formal->spelling = "ag";
    generic_default.kind = vhdl::AssociationKind::default_box;
    generic_default.source = generic_value.source;
    vhdl_instance.generic_map.push_back(generic_default);
    vhdl::Association vhdl_open_port;
    vhdl_open_port.formal.emplace();
    vhdl_open_port.formal->spelling = "payload";
    vhdl_open_port.kind = vhdl::AssociationKind::open;
    vhdl_open_port.source = generic_value.source;
    vhdl_instance.port_map.push_back(vhdl_open_port);
    const CompiledInstanceView vhdl_view { nullptr, &vhdl_instance };
    const auto vhdl = resolve_specialized_hir_associations(design,
        fixture.vhdl_architecture, vhdl_view,
        SpecializedHirAssociationSurface::parameters, &*parent_vhdl);
    assert(vhdl);
    assert(vhdl.bindings.size() == 3U);
    assert(vhdl.bindings[0].formal == fixture.entity_generic);
    assert(vhdl.bindings[0].identity == "11");
    assert(vhdl.bindings[1].formal == fixture.entity_type_generic);
    assert(vhdl.bindings[1].identity.starts_with("vhdl-type-v1;"));
    assert(vhdl.bindings[2].formal == fixture.architecture_generic);
    assert(vhdl.bindings[2].identity.empty());
    const auto vhdl_ports = resolve_specialized_hir_associations(design,
        fixture.vhdl_architecture, vhdl_view,
        SpecializedHirAssociationSurface::ports, &*parent_vhdl);
    assert(vhdl_ports);
    assert(vhdl_ports.bindings.size() == 1U);
    assert(vhdl_ports.bindings.front().formal == vhdl_port);
    assert(vhdl_ports.bindings.front().kind
        == SpecializedHirAssociationKind::open);
}

void test_specialized_hir_unit()
{
    const auto fixture = make_specialization_fixture();
    const auto& design = fixture.design;
    const auto any_record = [](const auto&) { return true; };

    const std::vector<SpecializedHirActualIdentity> sv_actuals {
        { fixture.type_parameter, "logic[7:0]" },
        { fixture.parameter, "7" },
    };
    auto systemverilog = make_specialized_hir_unit(
        design, fixture.systemverilog_unit, sv_actuals);
    assert(systemverilog);
    assert(&systemverilog->design() == &design);
    assert(systemverilog->unit() == fixture.systemverilog_unit);
    assert(systemverilog->scope()
        == design.units()[fixture.systemverilog_unit.value()].scope);
    assert(systemverilog->language() == Language::system_verilog);
    assert(systemverilog->specialization().actual_identities.front().declaration
        == fixture.parameter);
    assert(systemverilog->specialization().actual_identities.back().declaration
        == fixture.type_parameter);
    const std::vector<SpecializedHirNamedIdentity> named_sv_actuals {
        { "T", "logic[7:0]" },
        { "LOCAL", "ignored" },
        { "T", "ignored duplicate" },
    };
    const std::vector<SpecializedHirNamedIdentity> fallback_sv_actuals {
        { "P", "7" },
        { "T", "ignored fallback" },
        { "missing", "ignored" },
    };
    const auto named_systemverilog = make_specialized_hir_unit(
        design, fixture.systemverilog_unit,
        named_sv_actuals, fallback_sv_actuals);
    assert(named_systemverilog);
    assert(named_systemverilog->specialization()
        == systemverilog->specialization());

    const auto& sv_graph_declaration = record_in_unit(design,
        design.systemverilog_hir.declarations(),
        design.semantics.declarations(), fixture.systemverilog_unit,
        [](const auto& declaration) {
            return declaration.name == "payload";
        });
    const auto& sv_parameter
        = *design.find_declaration(fixture.parameter)->systemverilog;
    const auto& sv_type = record_in_unit(design,
        design.systemverilog_hir.types(), design.semantics.types(),
        fixture.systemverilog_unit,
        [](const auto& type) { return type.name == "payload_t"; });
    const auto& sv_expression = record_in_unit(design,
        design.systemverilog_hir.expressions(),
        design.semantics.expression_identities(),
        fixture.systemverilog_unit,
        [](const auto& expression) { return expression.text == "5"; });
    const auto& sv_statement = record_in_unit(design,
        design.systemverilog_hir.statements(),
        design.semantics.statement_identities(),
        fixture.systemverilog_unit, any_record);
    const auto& sv_process = record_in_unit(design,
        design.systemverilog_hir.processes(),
        design.semantics.process_identities(),
        fixture.systemverilog_unit, any_record);
    const auto& sv_instance = record_in_unit(design,
        design.systemverilog_hir.instances(), design.semantics.instances(),
        fixture.systemverilog_unit, any_record);
    const auto sv_instances = systemverilog->instances();
    assert(sv_instances.size() == 4);
    assert(sv_instances.front().systemverilog == &sv_instance);
    assert(sv_instances.front().vhdl == nullptr);
    assert(systemverilog->instance_ids().size() == 4);
    assert(std::ranges::is_sorted(systemverilog->instance_ids()));
    assert(systemverilog->instance_ids().front() == sv_instance.id);
    const auto parameter_generate_instances
        = systemverilog->generate_instance_ids(
            fixture.parameter_generate);
    assert(parameter_generate_instances.size() == 1);
    assert(parameter_generate_instances.front()
        == fixture.parameter_generate_instance);
    const auto nested_systemverilog_instances
        = systemverilog->generate_instance_ids(
            fixture.nested_systemverilog_generate);
    assert(nested_systemverilog_instances.size() == 1);
    assert(nested_systemverilog_instances.front()
        == fixture.nested_systemverilog_instance);
    const auto type_generate_instances
        = systemverilog->generate_instances(fixture.type_generate);
    assert(type_generate_instances.size() == 1);
    assert(type_generate_instances.front().systemverilog
        == design.find_instance(
            fixture.type_generate_instance)->systemverilog);
    assert(systemverilog->generate_instance_ids(
        fixture.entity_generate).empty());
    assert(systemverilog->selected_generate_instance_ids().empty());
    assert(systemverilog->find_instance(
        sv_instance.scope, sv_instance.name)->systemverilog
        == &sv_instance);
    const auto& vhdl_entity_declaration = record_in_unit(design,
        design.vhdl_hir.declarations(), design.semantics.declarations(),
        fixture.vhdl_entity,
        [](const auto& declaration) {
            return declaration.name == "payload";
        });

    auto invalid_sv_declaration = sv_graph_declaration;
    invalid_sv_declaration.source = SourceSpanId { };
    assert(!systemverilog->replace(invalid_sv_declaration));
    invalid_sv_declaration = sv_graph_declaration;
    invalid_sv_declaration.scope
        = design.units()[fixture.vhdl_entity.value()].scope;
    assert(!systemverilog->replace(invalid_sv_declaration));
    invalid_sv_declaration = sv_graph_declaration;
    invalid_sv_declaration.id = DeclarationId::from_index(1000);
    assert(!systemverilog->replace(invalid_sv_declaration));
    auto foreign_sv_declaration
        = *design.find_declaration(
             fixture.foreign_parameter)->systemverilog;
    foreign_sv_declaration.name = "rejected_foreign";
    assert(!systemverilog->replace(foreign_sv_declaration));
    assert(!systemverilog->replace(vhdl_entity_declaration));

    auto specialized_sv_declaration = sv_graph_declaration;
    specialized_sv_declaration.name = "specialized_payload";
    auto specialized_sv_parameter = sv_parameter;
    specialized_sv_parameter.name = "specialized_parameter";
    assert(systemverilog->replace(specialized_sv_declaration));
    assert(systemverilog->replace(specialized_sv_parameter));
    assert(std::ranges::is_sorted(
        systemverilog->systemverilog_declarations(), { },
        &sv::Declaration::id));
    assert(!systemverilog->replace(specialized_sv_parameter));

    auto specialized_sv_type = sv_type;
    specialized_sv_type.name = "specialized_payload_t";
    auto invalid_sv_type = specialized_sv_type;
    invalid_sv_type.declaration = fixture.foreign_parameter;
    assert(!systemverilog->replace(invalid_sv_type));
    assert(systemverilog->replace(specialized_sv_type));
    auto specialized_sv_expression = sv_expression;
    specialized_sv_expression.text = "37";
    assert(systemverilog->replace(specialized_sv_expression));
    auto specialized_sv_statement = sv_statement;
    specialized_sv_statement.label = "specialized_statement";
    assert(systemverilog->replace(specialized_sv_statement));
    auto specialized_sv_process = sv_process;
    specialized_sv_process.name = "specialized_process";
    assert(systemverilog->replace(specialized_sv_process));
    auto specialized_sv_instance = sv_instance;
    specialized_sv_instance.name = "specialized_instance";
    assert(systemverilog->replace(specialized_sv_instance));
    auto specialized_generated_instance
        = *design.find_instance(
             fixture.parameter_generate_instance)->systemverilog;
    specialized_generated_instance.name = "specialized_generated_child";
    assert(systemverilog->replace(specialized_generated_instance));
    const auto specialized_sv_instances = systemverilog->instances();
    assert(specialized_sv_instances.size() == 4);
    assert(specialized_sv_instances.front().systemverilog
        == systemverilog->find_instance(sv_instance.id)->systemverilog);
    assert(specialized_sv_instances.front().systemverilog
        != &sv_instance);
    assert(!systemverilog->find_instance(
        sv_instance.scope, sv_instance.name));
    assert(systemverilog->find_instance(
        sv_instance.scope, "specialized_instance"));
    const auto effective_parameter_generate_instances
        = systemverilog->generate_instances(fixture.parameter_generate);
    assert(effective_parameter_generate_instances.size() == 1);
    assert(effective_parameter_generate_instances.front().systemverilog
        == systemverilog->find_instance(
            fixture.parameter_generate_instance)->systemverilog);
    assert(effective_parameter_generate_instances.front()
               .systemverilog->name
        == "specialized_generated_child");

    const auto found_sv_declaration
        = systemverilog->find_declaration(sv_graph_declaration.id);
    assert(found_sv_declaration
        && found_sv_declaration->systemverilog->name
            == "specialized_payload"
        && found_sv_declaration->vhdl == nullptr);
    assert(found_sv_declaration->systemverilog->source
        == sv_graph_declaration.source);
    assert(found_sv_declaration->systemverilog->origin
        == sv_graph_declaration.origin);
    assert(systemverilog->find_type(sv_type.id)->systemverilog->name
        == "specialized_payload_t");
    assert(systemverilog->find_expression(sv_expression.id)
               ->systemverilog->text
        == "37");
    assert(systemverilog->find_statement(sv_statement.id)
               ->systemverilog->label
        == "specialized_statement");
    assert(systemverilog->find_process(sv_process.id)
               ->systemverilog->name
        == "specialized_process");
    assert(systemverilog->find_instance(sv_instance.id)
               ->systemverilog->name
        == "specialized_instance");
    assert(sv_graph_declaration.name == "payload");
    const auto fallback_foreign
        = systemverilog->find_declaration(fixture.foreign_parameter);
    assert(fallback_foreign
        && fallback_foreign->systemverilog->name == "FOREIGN");
    assert(!systemverilog->find_expression(
        ExpressionId::from_index(1000)));
    std::unordered_map<std::string, SpecializedHirUnit> specialization_cache;
    assert(specialization_cache.try_emplace(
        "sv:work.SpecializedSv|P=7|T=logic[7:0]",
        *systemverilog).second);
    const auto cached_systemverilog = specialization_cache.at(
        "sv:work.SpecializedSv|P=7|T=logic[7:0]");
    assert(&cached_systemverilog.design() == &design);
    assert(cached_systemverilog.specialization()
        == systemverilog->specialization());
    assert(cached_systemverilog.find_expression(sv_expression.id)
               ->systemverilog->text
        == "37");

    assert(systemverilog->select_generate(fixture.type_generate));
    assert(systemverilog->select_generate(fixture.parameter_generate));
    assert(std::ranges::is_sorted(
        systemverilog->selected_generates()));
    assert(systemverilog->selected_generate_instance_ids().size() == 2);
    assert(std::ranges::is_sorted(
        systemverilog->selected_generate_instance_ids()));
    assert(std::ranges::find(
        systemverilog->selected_generate_instance_ids(),
        fixture.nested_systemverilog_instance)
        == systemverilog->selected_generate_instance_ids().end());
    const auto selected_systemverilog_instances
        = systemverilog->selected_generate_instances();
    assert(selected_systemverilog_instances.size() == 2);
    assert(std::ranges::any_of(selected_systemverilog_instances,
        [&](const auto& instance) {
            return instance.systemverilog != nullptr
                && instance.systemverilog->name
                    == "specialized_generated_child";
        }));
    assert(systemverilog->select_generate(
        fixture.nested_systemverilog_generate));
    assert(systemverilog->selected_generate_instance_ids().size() == 3);
    assert(!systemverilog->select_generate(fixture.type_generate));
    assert(!systemverilog->select_generate(fixture.entity_generate));
    assert(!systemverilog->select_generate(sv_graph_declaration.id));

    const std::vector<SpecializedHirActualIdentity> vhdl_actuals {
        { fixture.architecture_generic, "13" },
        { fixture.entity_generic, "11" },
    };
    auto specialized_vhdl = make_specialized_hir_unit(
        design, fixture.vhdl_architecture, vhdl_actuals);
    assert(specialized_vhdl);
    assert(specialized_vhdl->language() == Language::vhdl);
    assert(specialized_vhdl->unit() == fixture.vhdl_architecture);
    const std::vector<SpecializedHirNamedIdentity> named_vhdl_actuals {
        { "g", "11" },
        { "missing", "ignored" },
    };
    const std::vector<SpecializedHirNamedIdentity> fallback_vhdl_actuals {
        { "AG", "13" },
        { "G", "ignored fallback" },
    };
    const auto named_vhdl = make_specialized_hir_unit(
        design, fixture.vhdl_architecture,
        named_vhdl_actuals, fallback_vhdl_actuals);
    assert(named_vhdl);
    assert(named_vhdl->specialization()
        == specialized_vhdl->specialization());

    const auto& vhdl_architecture_declaration = record_in_unit(design,
        design.vhdl_hir.declarations(), design.semantics.declarations(),
        fixture.vhdl_architecture,
        [](const auto& declaration) {
            return declaration.name == "payload";
        });
    const auto& vhdl_entity_type = record_in_unit(design,
        design.vhdl_hir.types(), design.semantics.types(),
        fixture.vhdl_entity,
        [](const auto& type) { return type.name == "payload_t"; });
    const auto& vhdl_entity_expression = record_in_unit(design,
        design.vhdl_hir.expressions(),
        design.semantics.expression_identities(), fixture.vhdl_entity,
        [](const auto& expression) { return expression.text == "17"; });
    vhdl::SubtypeIndication bound_vector;
    bound_vector.type_mark.spelling = "bit_vector";
    vhdl::RangeConstraint bound_range;
    bound_range.left_expression = fixture.entity_expression;
    bound_range.right = 0;
    bound_range.descending = true;
    bound_vector.constraints.push_back(bound_range);
    CompiledActualBinding bound_actual;
    bound_actual.formal = fixture.entity_generic;
    bound_actual.expression = vhdl_entity_expression.id;
    const std::vector<CompiledBindingFrame> bound_frames {
        { bound_actual }
    };
    const CompiledDesignResolver bound_resolver {
        *specialized_vhdl, bound_frames
    };
    const auto bound_effective = bound_resolver.effective_vhdl_subtype(
        bound_vector, specialized_vhdl->scope());
    assert(bound_effective
        && bound_effective->executable_width == 18U
        && bound_effective->constraints.size() == 1U
        && bound_effective->constraints.front().left == 17
        && bound_effective->constraints.front().right == 0
        && bound_effective->constraints.front().left_expression
            == fixture.entity_expression);
    const auto& vhdl_entity_statement = record_in_unit(design,
        design.vhdl_hir.statements(),
        design.semantics.statement_identities(), fixture.vhdl_entity,
        any_record);
    const auto& vhdl_entity_process = record_in_unit(design,
        design.vhdl_hir.processes(),
        design.semantics.process_identities(), fixture.vhdl_entity,
        any_record);
    const auto& vhdl_entity_instance = record_in_unit(design,
        design.vhdl_hir.instances(), design.semantics.instances(),
        fixture.vhdl_entity, any_record);
    const auto& vhdl_architecture_instance = record_in_unit(design,
        design.vhdl_hir.instances(), design.semantics.instances(),
        fixture.vhdl_architecture, any_record);
    const auto base_vhdl_instances = specialized_vhdl->instances();
    assert(base_vhdl_instances.size() == 5);
    assert(std::ranges::any_of(base_vhdl_instances,
        [&](const auto& instance) {
            return instance.vhdl == &vhdl_entity_instance;
        }));
    assert(std::ranges::any_of(base_vhdl_instances,
        [&](const auto& instance) {
            return instance.vhdl == &vhdl_architecture_instance;
        }));
    assert(std::ranges::is_sorted(specialized_vhdl->instance_ids()));
    const auto entity_generate_instances
        = specialized_vhdl->generate_instance_ids(
            fixture.entity_generate);
    assert(entity_generate_instances.size() == 1);
    assert(entity_generate_instances.front()
        == fixture.entity_generate_instance);
    const auto nested_vhdl_instances
        = specialized_vhdl->generate_instances(
            fixture.nested_vhdl_generate);
    assert(nested_vhdl_instances.size() == 1);
    assert(nested_vhdl_instances.front().vhdl
        == design.find_instance(fixture.nested_vhdl_instance)->vhdl);
    const auto architecture_generate_instances
        = specialized_vhdl->generate_instance_ids(
            fixture.architecture_generate);
    assert(architecture_generate_instances.size() == 1);
    assert(architecture_generate_instances.front()
        == fixture.architecture_generate_instance);
    assert(specialized_vhdl->find_instance(vhdl_entity_instance.scope,
        "CHILD")->vhdl == &vhdl_entity_instance);

    auto invalid_vhdl_process = vhdl_entity_process;
    invalid_vhdl_process.origin = OriginId { };
    assert(!specialized_vhdl->replace(invalid_vhdl_process));
    assert(!specialized_vhdl->replace(sv_graph_declaration));
    auto foreign_vhdl_declaration
        = *design.find_declaration(fixture.foreign_generic)->vhdl;
    foreign_vhdl_declaration.name = "rejected_foreign";
    assert(!specialized_vhdl->replace(foreign_vhdl_declaration));

    auto specialized_vhdl_architecture = vhdl_architecture_declaration;
    specialized_vhdl_architecture.name = "specialized_architecture_payload";
    auto specialized_vhdl_entity = vhdl_entity_declaration;
    specialized_vhdl_entity.name = "specialized_entity_payload";
    assert(specialized_vhdl->replace(specialized_vhdl_architecture));
    assert(specialized_vhdl->replace(specialized_vhdl_entity));
    assert(std::ranges::is_sorted(
        specialized_vhdl->vhdl_declarations(), { },
        &vhdl::Declaration::id));
    assert(!specialized_vhdl->replace(specialized_vhdl_entity));

    auto specialized_vhdl_type = vhdl_entity_type;
    specialized_vhdl_type.name = "specialized_entity_payload_t";
    assert(specialized_vhdl->replace(specialized_vhdl_type));
    auto specialized_vhdl_expression = vhdl_entity_expression;
    specialized_vhdl_expression.text = "41";
    assert(specialized_vhdl->replace(specialized_vhdl_expression));
    auto specialized_vhdl_statement = vhdl_entity_statement;
    specialized_vhdl_statement.label = "specialized_statement";
    assert(specialized_vhdl->replace(specialized_vhdl_statement));
    auto specialized_vhdl_process = vhdl_entity_process;
    specialized_vhdl_process.name = "specialized_process";
    assert(specialized_vhdl->replace(specialized_vhdl_process));
    auto specialized_vhdl_instance = vhdl_entity_instance;
    specialized_vhdl_instance.name = "specialized_instance";
    assert(specialized_vhdl->replace(specialized_vhdl_instance));
    const auto effective_vhdl_instances = specialized_vhdl->instances();
    assert(effective_vhdl_instances.size() == 5);
    assert(std::ranges::any_of(effective_vhdl_instances,
        [&](const auto& instance) {
            return instance.vhdl != nullptr
                && instance.vhdl->name == "specialized_instance";
        }));
    assert(specialized_vhdl->find_instance(vhdl_entity_instance.scope,
        "SPECIALIZED_INSTANCE"));
    assert(specialized_vhdl->find_instance(
        vhdl_architecture_instance.scope,
        vhdl_architecture_instance.name)->vhdl
        == &vhdl_architecture_instance);

    const auto found_vhdl_declaration
        = specialized_vhdl->find_declaration(vhdl_entity_declaration.id);
    assert(found_vhdl_declaration
        && found_vhdl_declaration->systemverilog == nullptr
        && found_vhdl_declaration->vhdl->name
            == "specialized_entity_payload");
    assert(specialized_vhdl->find_type(vhdl_entity_type.id)->vhdl->name
        == "specialized_entity_payload_t");
    assert(specialized_vhdl->find_expression(
               vhdl_entity_expression.id)->vhdl->text
        == "41");
    assert(specialized_vhdl->find_statement(
               vhdl_entity_statement.id)->vhdl->label
        == "specialized_statement");
    assert(specialized_vhdl->find_process(
               vhdl_entity_process.id)->vhdl->name
        == "specialized_process");
    assert(specialized_vhdl->find_instance(
               vhdl_entity_instance.id)->vhdl->name
        == "specialized_instance");
    assert(vhdl_entity_declaration.name == "payload");
    const auto base_foreign_generic
        = design.find_declaration(fixture.foreign_generic);
    const auto fallback_foreign_generic
        = specialized_vhdl->find_declaration(fixture.foreign_generic);
    assert(base_foreign_generic && fallback_foreign_generic);
    assert(fallback_foreign_generic->systemverilog == nullptr);
    assert(fallback_foreign_generic->vhdl
        == base_foreign_generic->vhdl);
    assert(!specialized_vhdl->find_declaration(
        DeclarationId::from_index(1000)));

    assert(specialized_vhdl->select_generate(
        fixture.architecture_generate));
    assert(specialized_vhdl->select_generate(fixture.entity_generate));
    assert(std::ranges::is_sorted(
        specialized_vhdl->selected_generates()));
    assert(specialized_vhdl->selected_generate_instance_ids().size() == 2);
    assert(std::ranges::find(
        specialized_vhdl->selected_generate_instance_ids(),
        fixture.nested_vhdl_instance)
        == specialized_vhdl->selected_generate_instance_ids().end());
    assert(specialized_vhdl->select_generate(
        fixture.nested_vhdl_generate));
    assert(specialized_vhdl->selected_generate_instances().size() == 3);
    assert(!specialized_vhdl->select_generate(
        fixture.architecture_generate));
    assert(!specialized_vhdl->select_generate(
        fixture.parameter_generate));
    assert(!specialized_vhdl->select_generate(
        vhdl_entity_declaration.id));

    assert(!make_specialized_hir_unit(
        design, fixture.systemverilog_unit,
        std::vector<SpecializedHirActualIdentity> {
            { fixture.foreign_parameter, "1" } }));
}

void test_specialized_hir_parameter_bit_selection()
{
    LinkBundleBuilder builder { "parameter-bit-selection.sv" };
    const auto unit = builder.add_systemverilog_unit(
        UnitKind::verilog_module, sv::UnitKind::module,
        "parameter_bit_selection");
    const auto scope = builder.model.units()[unit.value()].scope;

    const auto add_expression
        = [&](const sv::ExpressionKind kind, std::string text,
              std::vector<ExpressionId> operands = { },
              const std::optional<DeclarationId> selected = std::nullopt) {
              const auto id = builder.model.add_expression_identity(
                  scope, builder.source, builder.origin);
              sv::Expression expression;
              expression.id = id;
              expression.scope = scope;
              expression.kind = kind;
              expression.text = std::move(text);
              expression.operands = std::move(operands);
              expression.source = builder.source;
              expression.origin = builder.origin;
              if (selected) {
                  expression.referenced_name.emplace();
                  expression.referenced_name->spelling = expression.text;
                  expression.referenced_name->selected = selected;
              }
              builder.systemverilog.mutable_expressions().push_back(
                  std::move(expression));
              return id;
          };
    const auto add_parameter
        = [&](std::string name, const std::int64_t left,
              const std::int64_t right, std::string literal,
              const bool signed_value = false) {
              const auto initializer = add_expression(
                  sv::ExpressionKind::integer_literal,
                  std::move(literal));
              const auto declaration = builder.model.add_declaration(
                  scope, DeclarationKind::constant, name,
                  builder.source, builder.origin);
              sv::Declaration parameter;
              parameter.id = declaration;
              parameter.scope = scope;
              parameter.form = sv::DeclarationForm::parameter;
              parameter.name = name;
              parameter.initializer = initializer;
              parameter.source = builder.source;
              parameter.origin = builder.origin;
              parameter.type.emplace();
              parameter.type->target.spelling = "logic";
              parameter.type->target.source = builder.source;
              parameter.type->packed_range.emplace();
              parameter.type->packed_range->left = left;
              parameter.type->packed_range->right = right;
              parameter.type->packed_range->descending = left >= right;
              parameter.type->packed_range->source = builder.source;
              parameter.type->executable_width = static_cast<std::uint64_t>(
                  left >= right ? left - right + 1 : right - left + 1);
              parameter.type->signed_value = signed_value;
              parameter.type->four_state = true;
              builder.systemverilog.mutable_declarations().push_back(
                  std::move(parameter));
              builder.systemverilog_unit(unit).declarations.push_back(
                  declaration);
              return std::pair { declaration,
                  add_expression(sv::ExpressionKind::name,
                      std::move(name), { }, declaration) };
          };
    const auto add_index = [&](const ExpressionId base,
                               const std::int64_t value) {
        const auto index = add_expression(
            sv::ExpressionKind::integer_literal,
            std::to_string(value));
        return add_expression(sv::ExpressionKind::index, "[]",
            { base, index });
    };
    const auto add_slice = [&](const ExpressionId base, std::string text,
                               const std::int64_t first,
                               const std::int64_t second) {
        const auto first_expression = add_expression(
            sv::ExpressionKind::integer_literal, std::to_string(first));
        const auto second_expression = add_expression(
            sv::ExpressionKind::integer_literal, std::to_string(second));
        return add_expression(sv::ExpressionKind::slice,
            std::move(text), { base, first_expression, second_expression });
    };

    const auto [descending_parameter, descending]
        = add_parameter("DESCENDING", 7, 0, "8'b10000000");
    const auto [ascending_parameter, ascending]
        = add_parameter("ASCENDING", 0, 7, "8'b10000000");
    const auto [unknown_parameter, unknown]
        = add_parameter("UNKNOWN", 3, 0, "4'b1x0z");
    const auto [signed_parameter, signed_bits]
        = add_parameter("SIGNED_BITS", 7, 0, "8'sh8", true);
    const auto [extended_x_parameter, extended_x]
        = add_parameter("EXTENDED_X", 7, 0, "8'hx");
    const auto [extended_z_parameter, extended_z]
        = add_parameter("EXTENDED_Z", 7, 0, "8'hz");
    const auto [converted_signed_parameter, converted_signed]
        = add_parameter("CONVERTED_SIGNED", 7, 0, "4'shf");
    const auto [decimal_parameter, decimal]
        = add_parameter("DECIMAL", 7, 0, "5");
    const auto [negative_parameter, negative]
        = add_parameter("NEGATIVE", -1, -4, "4'b1000");
    const auto [field_poly_parameter, field_poly]
        = add_parameter("FIELD_POLY", 8, 0, "9'b100011101");
    const auto [asymmetric_descending_parameter, asymmetric_descending]
        = add_parameter("ASYMMETRIC_DESCENDING", 7, 0,
            "8'b10010110");
    const auto [asymmetric_ascending_parameter, asymmetric_ascending]
        = add_parameter("ASYMMETRIC_ASCENDING", 0, 7,
            "8'b10010110");
    const auto [extreme_parameter, extreme]
        = add_parameter("EXTREME", 3, 0, "4'b1010");
    auto extreme_record = std::ranges::find(
        builder.systemverilog.mutable_declarations(), extreme_parameter,
        &sv::Declaration::id);
    assert(extreme_record
        != builder.systemverilog.mutable_declarations().end());
    extreme_record->type->packed_range->left
        = std::numeric_limits<std::int64_t>::min();
    extreme_record->type->packed_range->right
        = std::numeric_limits<std::int64_t>::max();
    extreme_record->type->packed_range->descending = false;
    (void)descending_parameter;
    (void)ascending_parameter;
    (void)unknown_parameter;
    (void)signed_parameter;
    (void)extended_x_parameter;
    (void)extended_z_parameter;
    (void)converted_signed_parameter;
    (void)decimal_parameter;
    (void)negative_parameter;
    (void)field_poly_parameter;
    (void)asymmetric_descending_parameter;
    (void)asymmetric_ascending_parameter;
    const auto descending_left = add_index(descending, 7);
    const auto descending_right = add_index(descending, 0);
    const auto ascending_left = add_index(ascending, 0);
    const auto ascending_right = add_index(ascending, 7);
    const auto unknown_x = add_index(unknown, 2);
    const auto unknown_z = add_index(unknown, 0);
    const auto out_of_range = add_index(unknown, 4);
    const auto signed_sign_bit = add_index(signed_bits, 7);
    const auto signed_value_bit = add_index(signed_bits, 3);
    const auto extended_x_bit = add_index(extended_x, 7);
    const auto extended_z_bit = add_index(extended_z, 7);
    const auto converted_signed_bit = add_index(converted_signed, 7);
    const auto decimal_bit = add_index(decimal, 2);
    const auto negative_left = add_index(negative, -1);
    const auto extreme_left = add_index(
        extreme, std::numeric_limits<std::int64_t>::min());
    const auto descending_slice = add_slice(descending, ":", 7, 4);
    const auto ascending_slice = add_slice(ascending, ":", 0, 3);
    const auto unknown_slice = add_slice(unknown, "-:", 2, 3);
    const auto out_of_range_slice = add_slice(unknown, "+:", 2, 3);
    const auto field_poly_slice = add_slice(field_poly, ":", 7, 0);
    const auto descending_indexed_up = add_slice(
        asymmetric_descending, "+:", 4, 4);
    const auto descending_indexed_down = add_slice(
        asymmetric_descending, "-:", 7, 4);
    const auto ascending_indexed_up = add_slice(
        asymmetric_ascending, "+:", 0, 4);
    const auto ascending_indexed_down = add_slice(
        asymmetric_ascending, "-:", 3, 4);
    const auto invalid_descending_slice = add_slice(
        asymmetric_descending, ":", 0, 3);
    const auto invalid_ascending_slice = add_slice(
        asymmetric_ascending, ":", 3, 0);

    const auto add_variable = [&](const DeclarationKind kind,
                                  const sv::DeclarationForm form,
                                  std::string name,
                                  const std::optional<std::uint64_t> width
                                      = std::nullopt) {
        const auto declaration = builder.model.add_declaration(
            scope, kind, name, builder.source, builder.origin);
        sv::Declaration record;
        record.id = declaration;
        record.scope = scope;
        record.form = form;
        record.name = std::move(name);
        record.source = builder.source;
        record.origin = builder.origin;
        if (width) {
            record.type.emplace();
            record.type->target.spelling = "logic";
            record.type->target.source = builder.source;
            record.type->packed_range.emplace();
            record.type->packed_range->left
                = static_cast<std::int64_t>(*width - 1U);
            record.type->packed_range->right = 0;
            record.type->packed_range->descending = true;
            record.type->packed_range->source = builder.source;
            record.type->executable_width = width;
            record.type->four_state = true;
        }
        builder.systemverilog.mutable_declarations().push_back(
            std::move(record));
        return declaration;
    };
    const auto lhs = add_variable(DeclarationKind::constant,
        sv::DeclarationForm::variable, "lhs");
    const auto rhs = add_variable(DeclarationKind::constant,
        sv::DeclarationForm::variable, "rhs");
    const auto a = add_variable(DeclarationKind::variable,
        sv::DeclarationForm::variable, "a");
    const auto b = add_variable(DeclarationKind::variable,
        sv::DeclarationForm::variable, "b");
    const auto remainder = add_variable(DeclarationKind::variable,
        sv::DeclarationForm::variable, "remainder");
    const auto gcd = add_variable(DeclarationKind::function,
        sv::DeclarationForm::function, "rs_gcd");
    const auto name = [&](const std::string& spelling,
                          const DeclarationId declaration) {
        return add_expression(sv::ExpressionKind::name,
            spelling, { }, declaration);
    };
    const auto literal = [&](const std::int64_t value) {
        return add_expression(sv::ExpressionKind::integer_literal,
            std::to_string(value));
    };
    const auto binary = [&](const std::string& operation,
                            const ExpressionId left,
                            const ExpressionId right) {
        return add_expression(sv::ExpressionKind::binary,
            operation, { left, right });
    };
    const auto add_assignment = [&](const ExpressionId target,
                                    const ExpressionId value) {
        const auto id = builder.model.add_statement_identity(
            scope, builder.source, builder.origin);
        sv::Statement statement;
        statement.id = id;
        statement.scope = scope;
        statement.kind = sv::StatementKind::assignment;
        statement.target = target;
        statement.value = value;
        statement.source = builder.source;
        statement.origin = builder.origin;
        builder.systemverilog.mutable_statements().push_back(
            std::move(statement));
        return id;
    };
    const auto assign_a = add_assignment(name("a", a), name("lhs", lhs));
    const auto assign_b = add_assignment(name("b", b), name("rhs", rhs));
    const auto assign_remainder = add_assignment(name("remainder", remainder),
        binary("%", name("a", a), name("b", b)));
    const auto advance_a = add_assignment(name("a", a), name("b", b));
    const auto advance_b = add_assignment(
        name("b", b), name("remainder", remainder));
    const auto loop_id = builder.model.add_statement_identity(
        scope, builder.source, builder.origin);
    sv::Statement loop;
    loop.id = loop_id;
    loop.scope = scope;
    loop.kind = sv::StatementKind::loop;
    loop.loop_runtime = true;
    loop.condition = binary("!=", name("b", b), literal(0));
    loop.statements = { assign_remainder, advance_a, advance_b };
    loop.source = builder.source;
    loop.origin = builder.origin;
    builder.systemverilog.mutable_statements().push_back(std::move(loop));
    const auto assign_result = add_assignment(
        name("rs_gcd", gcd), name("a", a));
    auto gcd_record = std::ranges::find(
        builder.systemverilog.mutable_declarations(), gcd,
        &sv::Declaration::id);
    assert(gcd_record != builder.systemverilog.declarations().end());
    gcd_record->callable.emplace();
    gcd_record->callable->function = true;
    gcd_record->callable->formals = { lhs, rhs };
    gcd_record->children = { lhs, rhs, a, b, remainder };
    gcd_record->statements = {
        assign_a, assign_b, loop_id, assign_result };
    builder.systemverilog_unit(unit).declarations.push_back(gcd);
    const auto gcd_call = add_expression(sv::ExpressionKind::call,
        "rs_gcd", { literal(1), literal(255) }, gcd);
    const auto narrow = add_variable(DeclarationKind::function,
        sv::DeclarationForm::function, "narrow", 8U);
    const auto working = add_variable(DeclarationKind::variable,
        sv::DeclarationForm::variable, "working", 8U);
    const auto initialize_working = add_assignment(
        name("working", working), literal(128));
    const auto shift_working = add_assignment(name("working", working),
        binary("<<", name("working", working), literal(1)));
    const auto reduce_working = add_assignment(name("working", working),
        binary("^", name("working", working), literal(0x1d)));
    const auto assign_narrow = add_assignment(
        name("narrow", narrow), name("working", working));
    auto narrow_record = std::ranges::find(
        builder.systemverilog.mutable_declarations(), narrow,
        &sv::Declaration::id);
    assert(narrow_record != builder.systemverilog.declarations().end());
    narrow_record->callable.emplace();
    narrow_record->callable->function = true;
    narrow_record->children = { working };
    narrow_record->statements = { initialize_working, shift_working,
        reduce_working, assign_narrow };
    builder.systemverilog_unit(unit).declarations.push_back(narrow);
    const auto narrow_call = add_expression(sv::ExpressionKind::call,
        "narrow", { }, narrow);

    auto design = builder.finish();
    assert(design.valid());
    const std::array<SpecializedHirActualIdentity, 0> actuals { };
    const auto specialized = make_specialized_hir_unit(
        design, unit, actuals);
    assert(specialized);
    const auto assert_selected
        = [&](const ExpressionId expression, const std::string& bits,
              const std::optional<std::int64_t> integral,
              const std::optional<bool> truth) {
              const auto actual_bits
                  = specialized->evaluate_systemverilog_bits_expression(
                      expression);
              const auto actual_integral
                  = specialized->evaluate_integral_expression(expression);
              const auto actual_truth
                  = specialized->evaluate_systemverilog_truth_expression(
                      expression);
              if (actual_bits != std::optional<std::string> { bits }
                  || actual_integral != integral
                  || actual_truth != truth) {
                  std::cerr << "selected-bit expression "
                            << expression.value() << ": expected bits="
                            << bits << " integral="
                            << (integral ? std::to_string(*integral)
                                         : "unknown")
                            << " truth="
                            << (truth ? (*truth ? "true" : "false")
                                      : "unknown")
                            << ", actual bits="
                            << (actual_bits ? *actual_bits : "unknown")
                            << " integral="
                            << (actual_integral
                                    ? std::to_string(*actual_integral)
                                    : "unknown")
                            << " truth="
                            << (actual_truth
                                    ? (*actual_truth ? "true" : "false")
                                    : "unknown") << '\n';
              }
              assert(actual_bits == std::optional<std::string> { bits });
              assert(actual_integral == integral);
              assert(actual_truth == truth);
          };
    assert_selected(descending_left, "1", 1, true);
    assert_selected(descending_right, "0", 0, false);
    assert_selected(ascending_left, "1", 1, true);
    assert_selected(ascending_right, "0", 0, false);
    assert_selected(unknown_x, "x", std::nullopt, std::nullopt);
    assert_selected(unknown_z, "z", std::nullopt, std::nullopt);
    assert_selected(out_of_range, "x", std::nullopt, std::nullopt);
    assert_selected(signed_sign_bit, "0", 0, false);
    assert_selected(signed_value_bit, "1", 1, true);
    assert_selected(extended_x_bit, "x", std::nullopt, std::nullopt);
    assert_selected(extended_z_bit, "z", std::nullopt, std::nullopt);
    assert_selected(converted_signed_bit, "1", 1, true);
    assert_selected(decimal_bit, "1", 1, true);
    assert_selected(negative_left, "1", 1, true);
    assert_selected(extreme_left, "x", std::nullopt, std::nullopt);
    assert_selected(descending_slice, "1000", 8, true);
    assert_selected(ascending_slice, "1000", 8, true);
    assert_selected(
        unknown_slice, "x0z", std::nullopt, std::nullopt);
    assert_selected(out_of_range_slice, "x1x", std::nullopt, true);
    assert_selected(field_poly_slice, "00011101", 29, true);
    assert_selected(descending_indexed_up, "1001", 9, true);
    assert_selected(descending_indexed_down, "1001", 9, true);
    assert_selected(ascending_indexed_up, "1001", 9, true);
    assert_selected(ascending_indexed_down, "1001", 9, true);
    assert(!specialized->evaluate_systemverilog_bits_expression(
        invalid_descending_slice));
    assert(!specialized->evaluate_systemverilog_bits_expression(
        invalid_ascending_slice));
    assert(specialized->evaluate_integral_expression(gcd_call)
        == std::optional<std::int64_t> { 1 });
    assert(specialized->evaluate_integral_expression(narrow_call)
        == std::optional<std::int64_t> { 0x1d });
}

void test_specialized_hir_vhdl_while_function()
{
    LinkBundleBuilder builder { "while-function.vhd" };
    const auto unit = builder.add_vhdl_unit(
        UnitKind::vhdl_entity, vhdl::UnitKind::entity,
        "while_function");
    const auto scope = builder.model.units()[unit.value()].scope;
    const auto add_expression_in_scope
        = [&](const ScopeId expression_scope,
              const vhdl::ExpressionKind kind, std::string text,
              std::vector<ExpressionId> operands = { },
              const std::optional<DeclarationId> selected = std::nullopt) {
              const auto id = builder.model.add_expression_identity(
                  expression_scope, builder.source, builder.origin);
              vhdl::Expression expression;
              expression.id = id;
              expression.scope = expression_scope;
              expression.kind = kind;
              expression.text = std::move(text);
              expression.operands = std::move(operands);
              expression.source = builder.source;
              expression.origin = builder.origin;
              if (selected) {
                  expression.referenced_name.emplace();
                  expression.referenced_name->spelling = expression.text;
                  expression.referenced_name->canonical = expression.text;
                  expression.referenced_name->selected = selected;
              }
              builder.vhdl_hir.mutable_expressions().push_back(
                  std::move(expression));
              return id;
          };
    const auto add_expression
        = [&](const vhdl::ExpressionKind kind, std::string text,
              std::vector<ExpressionId> operands = { },
              const std::optional<DeclarationId> selected = std::nullopt) {
              return add_expression_in_scope(scope, kind,
                  std::move(text), std::move(operands), selected);
          };
    const auto literal = [&](const std::int64_t value) {
        return add_expression(vhdl::ExpressionKind::integer_literal,
            std::to_string(value));
    };
    const auto add_declaration
        = [&](const DeclarationKind kind,
              const vhdl::DeclarationForm form, std::string name,
              const std::optional<ExpressionId> initializer = std::nullopt) {
              const auto id = builder.model.add_declaration(
                  scope, kind, name, builder.source, builder.origin);
              vhdl::Declaration declaration;
              declaration.id = id;
              declaration.scope = scope;
              declaration.form = form;
              declaration.name = std::move(name);
              declaration.initializer = initializer;
              declaration.source = builder.source;
              declaration.origin = builder.origin;
              builder.vhdl_hir.mutable_declarations().push_back(
                  std::move(declaration));
              return id;
          };
    const auto n = add_declaration(DeclarationKind::constant,
        vhdl::DeclarationForm::constant, "n");
    const auto r = add_declaration(DeclarationKind::variable,
        vhdl::DeclarationForm::variable, "r", literal(0));
    const auto v = add_declaration(DeclarationKind::variable,
        vhdl::DeclarationForm::variable, "v", literal(1));
    const auto clog2 = add_declaration(DeclarationKind::function,
        vhdl::DeclarationForm::function, "clog2");
    const auto name = [&](const std::string& spelling,
                          const DeclarationId declaration) {
        return add_expression(vhdl::ExpressionKind::name,
            spelling, { }, declaration);
    };
    const auto binary = [&](const std::string& operation,
                            const ExpressionId left,
                            const ExpressionId right) {
        return add_expression(vhdl::ExpressionKind::binary,
            operation, { left, right });
    };
    const auto add_statement = [&](const vhdl::StatementKind kind) {
        const auto id = builder.model.add_statement_identity(
            scope, builder.source, builder.origin);
        vhdl::Statement statement;
        statement.id = id;
        statement.scope = scope;
        statement.kind = kind;
        statement.source = builder.source;
        statement.origin = builder.origin;
        builder.vhdl_hir.mutable_statements().push_back(
            std::move(statement));
        return id;
    };
    const auto double_v = add_statement(
        vhdl::StatementKind::variable_assignment);
    auto double_v_record = std::ranges::find(
        builder.vhdl_hir.mutable_statements(), double_v,
        &vhdl::Statement::id);
    double_v_record->target = name("v", v);
    double_v_record->value = binary("*", name("v", v), literal(2));
    const auto increment_r = add_statement(
        vhdl::StatementKind::variable_assignment);
    auto increment_r_record = std::ranges::find(
        builder.vhdl_hir.mutable_statements(), increment_r,
        &vhdl::Statement::id);
    increment_r_record->target = name("r", r);
    increment_r_record->value = binary("+", name("r", r), literal(1));
    const auto loop = add_statement(vhdl::StatementKind::loop);
    auto loop_record = std::ranges::find(
        builder.vhdl_hir.mutable_statements(), loop,
        &vhdl::Statement::id);
    loop_record->condition = binary("<", name("v", v), name("n", n));
    loop_record->statements = { double_v, increment_r };
    const auto return_r = add_statement(
        vhdl::StatementKind::return_statement);
    auto return_record = std::ranges::find(
        builder.vhdl_hir.mutable_statements(), return_r,
        &vhdl::Statement::id);
    return_record->value = name("r", r);
    auto function_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), clog2,
        &vhdl::Declaration::id);
    function_record->callable.emplace();
    function_record->callable->function = true;
    function_record->callable->pure = true;
    function_record->callable->defined = true;
    function_record->callable->formals = { n };
    function_record->children = { n, r, v };
    function_record->statements = { loop, return_r };
    builder.vhdl_unit(unit).declarations.push_back(clog2);
    const auto call = add_expression(vhdl::ExpressionKind::call,
        "clog2", { literal(5) }, clog2);
    const auto width = binary("-", call, literal(1));

    const auto default_poly = add_declaration(DeclarationKind::function,
        vhdl::DeclarationForm::function, "default_poly");
    const auto m = add_declaration(DeclarationKind::constant,
        vhdl::DeclarationForm::constant, "m");
    const auto runtime = add_declaration(DeclarationKind::variable,
        vhdl::DeclarationForm::variable, "runtime");
    const auto n_binding_name = name("n", n);
    const auto m_binding_name = name("m", m);
    const auto runtime_name = name("runtime", runtime);
    const auto bound_arithmetic = binary(
        "+", n_binding_name, literal(3));
    const auto return_11d = add_statement(
        vhdl::StatementKind::return_statement);
    auto return_11d_record = std::ranges::find(
        builder.vhdl_hir.mutable_statements(), return_11d,
        &vhdl::Statement::id);
    return_11d_record->value = literal(0x11d);
    const auto return_default = add_statement(
        vhdl::StatementKind::return_statement);
    auto return_default_record = std::ranges::find(
        builder.vhdl_hir.mutable_statements(), return_default,
        &vhdl::Statement::id);
    return_default_record->value = literal(7);
    const auto selection = add_statement(vhdl::StatementKind::selection);
    auto selection_record = std::ranges::find(
        builder.vhdl_hir.mutable_statements(), selection,
        &vhdl::Statement::id);
    selection_record->condition = name("m", m);
    vhdl::CaseAlternative matching;
    matching.choices = { literal(8) };
    matching.statements = { return_11d };
    matching.source = builder.source;
    vhdl::CaseAlternative others;
    others.statements = { return_default };
    others.is_default = true;
    others.source = builder.source;
    selection_record->alternatives = {
        std::move(matching), std::move(others) };
    auto default_poly_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), default_poly,
        &vhdl::Declaration::id);
    default_poly_record->callable.emplace();
    default_poly_record->callable->function = true;
    default_poly_record->callable->pure = true;
    default_poly_record->callable->defined = true;
    default_poly_record->callable->formals = { m };
    default_poly_record->children = { m };
    default_poly_record->statements = { selection };
    builder.vhdl_unit(unit).declarations.push_back(default_poly);
    const auto matching_call = add_expression(vhdl::ExpressionKind::call,
        "default_poly", { literal(8) }, default_poly);
    const auto default_call = add_expression(vhdl::ExpressionKind::call,
        "default_poly", { literal(3) }, default_poly);
    const auto add_failing_case_function
        = [&](std::string function_name,
              const std::vector<std::int64_t>& choices,
              const std::int64_t actual) {
              const auto function = add_declaration(
                  DeclarationKind::function,
                  vhdl::DeclarationForm::function, function_name);
              const auto formal = add_declaration(
                  DeclarationKind::constant,
                  vhdl::DeclarationForm::constant,
                  function_name + "_formal");
              const auto return_value = add_statement(
                  vhdl::StatementKind::return_statement);
              auto return_value_record = std::ranges::find(
                  builder.vhdl_hir.mutable_statements(), return_value,
                  &vhdl::Statement::id);
              return_value_record->value = literal(1);
              const auto case_statement = add_statement(
                  vhdl::StatementKind::selection);
              auto case_record = std::ranges::find(
                  builder.vhdl_hir.mutable_statements(), case_statement,
                  &vhdl::Statement::id);
              case_record->condition = name(
                  function_name + "_formal", formal);
              vhdl::CaseAlternative alternative;
              for (const auto choice : choices) {
                  alternative.choices.push_back(literal(choice));
              }
              alternative.statements = { return_value };
              alternative.source = builder.source;
              case_record->alternatives.push_back(
                  std::move(alternative));
              auto function_record = std::ranges::find(
                  builder.vhdl_hir.mutable_declarations(), function,
                  &vhdl::Declaration::id);
              function_record->callable.emplace();
              function_record->callable->function = true;
              function_record->callable->pure = true;
              function_record->callable->defined = true;
              function_record->callable->formals = { formal };
              function_record->children = { formal };
              function_record->statements = { case_statement };
              builder.vhdl_unit(unit).declarations.push_back(function);
              return add_expression(vhdl::ExpressionKind::call,
                  std::move(function_name), { literal(actual) }, function);
          };
    const auto unmatched_call = add_failing_case_function(
        "unmatched_case", { 8 }, 3);
    const auto duplicate_call = add_failing_case_function(
        "duplicate_case", { 8, 8 }, 8);

    const auto sum_function = add_declaration(
        DeclarationKind::function,
        vhdl::DeclarationForm::function, "sum_sized_range");
    const auto degree = add_declaration(DeclarationKind::constant,
        vhdl::DeclarationForm::constant, "degree");
    const auto degree_width = add_declaration(
        DeclarationKind::variable, vhdl::DeclarationForm::variable,
        "degree_width", binary("+", name("degree", degree), literal(1)));
    const auto total = add_declaration(DeclarationKind::variable,
        vhdl::DeclarationForm::variable, "total", literal(0));
    const auto sum_loop = add_statement(vhdl::StatementKind::loop);
    const auto sum_loop_scope = builder.model.add_scope(
        unit, scope, "<loop>", builder.source, builder.origin);
    const auto loop_index = builder.model.add_declaration(
        sum_loop_scope, DeclarationKind::constant, "i",
        builder.source, builder.origin);
    vhdl::Declaration loop_index_record;
    loop_index_record.id = loop_index;
    loop_index_record.scope = sum_loop_scope;
    loop_index_record.form = vhdl::DeclarationForm::constant;
    loop_index_record.name = "i";
    loop_index_record.object_class = vhdl::ObjectClass::constant;
    loop_index_record.source = builder.source;
    loop_index_record.origin = builder.origin;
    builder.vhdl_hir.mutable_declarations().push_back(
        std::move(loop_index_record));
    auto sum_loop_record = std::ranges::find(
        builder.vhdl_hir.mutable_statements(), sum_loop,
        &vhdl::Statement::id);
    sum_loop_record->loop_variable = "i";
    sum_loop_record->loop_initial = literal(0);
    sum_loop_record->loop_limit = binary(
        "-", name("degree", degree), literal(1));
    sum_loop_record->nested_scope = sum_loop_scope;
    sum_loop_record->declarations = { loop_index };
    const auto loop_name = [&](const std::string& spelling,
                               const DeclarationId declaration) {
        return add_expression_in_scope(sum_loop_scope,
            vhdl::ExpressionKind::name, spelling, { }, declaration);
    };
    const auto loop_binary = [&](const std::string& operation,
                                 const ExpressionId left,
                                 const ExpressionId right) {
        return add_expression_in_scope(sum_loop_scope,
            vhdl::ExpressionKind::binary, operation, { left, right });
    };
    const auto add_loop_statement = [&](const vhdl::StatementKind kind) {
        const auto id = builder.model.add_statement_identity(
            sum_loop_scope, builder.source, builder.origin);
        vhdl::Statement statement;
        statement.id = id;
        statement.scope = sum_loop_scope;
        statement.kind = kind;
        statement.source = builder.source;
        statement.origin = builder.origin;
        builder.vhdl_hir.mutable_statements().push_back(
            std::move(statement));
        return id;
    };
    const auto accumulate = add_loop_statement(
        vhdl::StatementKind::variable_assignment);
    auto accumulate_record = std::ranges::find(
        builder.vhdl_hir.mutable_statements(), accumulate,
        &vhdl::Statement::id);
    accumulate_record->target = loop_name("total", total);
    accumulate_record->value = loop_binary("+",
        loop_name("total", total),
        loop_binary("+", loop_name("degree_width", degree_width),
            loop_name("i", loop_index)));
    sum_loop_record = std::ranges::find(
        builder.vhdl_hir.mutable_statements(), sum_loop,
        &vhdl::Statement::id);
    sum_loop_record->statements = { accumulate };
    const auto return_total = add_statement(
        vhdl::StatementKind::return_statement);
    auto return_total_record = std::ranges::find(
        builder.vhdl_hir.mutable_statements(), return_total,
        &vhdl::Statement::id);
    return_total_record->value = name("total", total);
    auto sum_function_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), sum_function,
        &vhdl::Declaration::id);
    sum_function_record->callable.emplace();
    sum_function_record->callable->function = true;
    sum_function_record->callable->pure = true;
    sum_function_record->callable->defined = true;
    sum_function_record->callable->formals = { degree };
    sum_function_record->children = { degree, degree_width, total };
    sum_function_record->statements = { sum_loop, return_total };
    builder.vhdl_unit(unit).declarations.push_back(sum_function);
    const auto sized_sum_call = add_expression(
        vhdl::ExpressionKind::call, "sum_sized_range",
        { literal(4) }, sum_function);

    auto design = builder.finish();
    assert(design.valid());
    const std::array<SpecializedHirActualIdentity, 0> actuals { };
    const auto specialized = make_specialized_hir_unit(
        design, unit, actuals);
    assert(specialized);
    const SpecializedHirIntegralBinding arithmetic_binding =
        [&](const DeclarationId formal) -> std::optional<ExpressionId> {
            return formal == n
                ? std::optional<ExpressionId> { call }
                : std::nullopt;
        };
    assert(specialized->evaluate_integral_expression(
               bound_arithmetic, arithmetic_binding)
        == std::optional<std::int64_t> { 6 });

    const SpecializedHirIntegralBinding cycle_binding =
        [&](const DeclarationId formal) -> std::optional<ExpressionId> {
            if (formal == n) {
                return m_binding_name;
            }
            if (formal == m) {
                return n_binding_name;
            }
            return std::nullopt;
        };
    assert(!specialized->evaluate_integral_expression(
        n_binding_name, cycle_binding));

    const SpecializedHirIntegralBinding runtime_binding =
        [&](const DeclarationId formal) -> std::optional<ExpressionId> {
            return formal == n
                ? std::optional<ExpressionId> { runtime_name }
                : std::nullopt;
        };
    assert(!specialized->evaluate_integral_expression(
        n_binding_name, runtime_binding));
    assert(specialized->evaluate_integral_expression(call)
        == std::optional<std::int64_t> { 3 });
    assert(specialized->evaluate_integral_expression(width)
        == std::optional<std::int64_t> { 2 });
    assert(specialized->evaluate_integral_expression(matching_call)
        == std::optional<std::int64_t> { 0x11d });
    assert(specialized->evaluate_integral_expression(default_call)
        == std::optional<std::int64_t> { 7 });
    assert(!specialized->evaluate_integral_expression(unmatched_call));
    assert(!specialized->evaluate_integral_expression(duplicate_call));
    assert(specialized->evaluate_integral_expression(sized_sum_call)
        == std::optional<std::int64_t> { 26 });
}

void test_specialized_hir_vhdl_integer_exponentiation()
{
    LinkBundleBuilder builder { "integer-exponentiation.vhd" };
    const auto unit = builder.add_vhdl_unit(
        UnitKind::vhdl_entity, vhdl::UnitKind::entity,
        "integer_exponentiation");
    const auto scope = builder.model.units()[unit.value()].scope;
    const auto add_expression
        = [&](const vhdl::ExpressionKind kind, std::string text,
              std::vector<ExpressionId> operands = { },
              const std::optional<DeclarationId> selected = std::nullopt) {
              const auto id = builder.model.add_expression_identity(
                  scope, builder.source, builder.origin);
              vhdl::Expression expression;
              expression.id = id;
              expression.scope = scope;
              expression.kind = kind;
              expression.text = std::move(text);
              expression.operands = std::move(operands);
              expression.source = builder.source;
              expression.origin = builder.origin;
              if (selected) {
                  expression.referenced_name.emplace();
                  expression.referenced_name->spelling = expression.text;
                  expression.referenced_name->canonical = expression.text;
                  expression.referenced_name->selected = selected;
              }
              builder.vhdl_hir.mutable_expressions().push_back(
                  std::move(expression));
              return id;
          };
    const auto literal = [&](const std::int64_t value) {
        return add_expression(vhdl::ExpressionKind::integer_literal,
            std::to_string(value));
    };
    const auto binary = [&](const std::string& operation,
                            const ExpressionId left,
                            const ExpressionId right) {
        return add_expression(vhdl::ExpressionKind::binary,
            operation, { left, right });
    };
    const auto add_generic = [&](const std::string& name) {
        const auto id = builder.model.add_declaration(scope,
            DeclarationKind::generic, name, builder.source,
            builder.origin);
        vhdl::Declaration declaration;
        declaration.id = id;
        declaration.scope = scope;
        declaration.form = vhdl::DeclarationForm::generic_constant;
        declaration.object_class = vhdl::ObjectClass::constant;
        declaration.name = name;
        declaration.subtype.emplace();
        declaration.subtype->type_mark.spelling = "integer";
        declaration.subtype->domain = vhdl::ValueDomain::integer;
        declaration.source = builder.source;
        declaration.origin = builder.origin;
        builder.vhdl_hir.mutable_declarations().push_back(
            std::move(declaration));
        builder.vhdl_unit(unit).declarations.push_back(id);
        return id;
    };
    const auto m = add_generic("M");
    const auto n = add_generic("N");
    const auto depth = add_generic("DEPTH");
    const auto m_name = add_expression(
        vhdl::ExpressionKind::name, "M", { }, m);
    const auto n_name = add_expression(
        vhdl::ExpressionKind::name, "N", { }, n);
    const auto depth_name = add_expression(
        vhdl::ExpressionKind::name, "DEPTH", { }, depth);
    const auto one = literal(1);
    const auto two = literal(2);
    const auto depth_actual = binary(
        "**", two, m_name);
    const auto dependent_condition = binary("+",
        binary("-", binary("-", depth_actual, one), n_name), one);
    const auto negative_one = add_expression(
        vhdl::ExpressionKind::unary, "-", { one });
    const auto overflow_power = binary("**", two, literal(63));
    const auto negative_exponent_power = binary(
        "**", two, negative_one);
    const auto depth_dependent_condition = binary("+",
        binary("-", binary("-", depth_name, one), n_name), one);

    const auto generated = builder.model.add_declaration(scope,
        DeclarationKind::generate, "depth_positive",
        builder.source, builder.origin);
    vhdl::Declaration generated_declaration;
    generated_declaration.id = generated;
    generated_declaration.scope = scope;
    generated_declaration.form = vhdl::DeclarationForm::generated;
    generated_declaration.name = "depth_positive";
    generated_declaration.source = builder.source;
    generated_declaration.origin = builder.origin;
    builder.vhdl_hir.mutable_declarations().push_back(
        std::move(generated_declaration));
    builder.vhdl_unit(unit).declarations.push_back(generated);
    vhdl::GenerateRegion generate;
    generate.declaration = generated;
    generate.scope = builder.model.add_scope(unit, scope,
        "depth_positive", builder.source, builder.origin);
    generate.kind = vhdl::GenerateKind::conditional;
    generate.label = "depth_positive";
    generate.condition = dependent_condition;
    generate.source = builder.source;
    generate.origin = builder.origin;
    builder.vhdl_unit(unit).generates.push_back(std::move(generate));

    auto design = builder.finish();
    assert(design.valid());
    const std::vector<SpecializedHirActualIdentity> actuals {
        { m, "8" }, { n, "255" },
    };
    const auto specialized = make_specialized_hir_unit(
        design, unit, actuals);
    assert(specialized);
    const SpecializedHirIntegralBinding generic_actual_binding =
        [&](const DeclarationId formal)
            -> std::optional<ExpressionId> {
            return formal == depth
                ? std::optional<ExpressionId> { depth_actual }
                : std::nullopt;
        };
    assert(specialized->evaluate_integral_expression(
               depth_name, generic_actual_binding)
        == std::optional<std::int64_t> { 256 });
    assert(specialized->evaluate_integral_expression(
               dependent_condition)
        == std::optional<std::int64_t> { 1 });
    assert(specialized->evaluate_integral_expression(
               depth_dependent_condition, generic_actual_binding)
        == std::optional<std::int64_t> { 1 });
    assert(std::ranges::find(specialized->selected_generates(), generated)
        != specialized->selected_generates().end());
    assert(!specialized->evaluate_integral_expression(overflow_power));
    assert(!specialized->evaluate_integral_expression(
        negative_exponent_power));
}

void test_specialized_hir_vhdl_packed_constant_function()
{
    LinkBundleBuilder builder { "packed-constant-function.vhd" };
    const auto unit = builder.add_vhdl_unit(
        UnitKind::vhdl_entity, vhdl::UnitKind::entity,
        "packed_constant_function");
    const auto scope = builder.model.units()[unit.value()].scope;
    const auto numeric_std = builder.add_vhdl_unit(
        UnitKind::vhdl_package, vhdl::UnitKind::package,
        "numeric_std", {}, "ieee");
    const auto numeric_std_scope
        = builder.model.units()[numeric_std.value()].scope;
    const auto add_numeric_vector = [&](const std::string& type_name) {
        const auto declaration = builder.model.add_declaration(
            numeric_std_scope, DeclarationKind::type, type_name,
            builder.source, builder.origin);
        TypeReference base;
        base.source = builder.source;
        const auto type = builder.model.add_type(numeric_std_scope,
            TypeKind::declaration, type_name, base,
            builder.source, builder.origin);
        vhdl::Declaration type_record;
        type_record.id = declaration;
        type_record.scope = numeric_std_scope;
        type_record.form = vhdl::DeclarationForm::type;
        type_record.name = type_name;
        type_record.declared_type = type;
        type_record.source = builder.source;
        type_record.origin = builder.origin;
        builder.vhdl_hir.mutable_declarations().push_back(
            std::move(type_record));
        vhdl::TypeDefinition definition;
        definition.id = type;
        definition.declaration = declaration;
        definition.form = vhdl::TypeForm::array;
        definition.name = type_name;
        definition.element_subtype.emplace();
        definition.element_subtype->type_mark.spelling = "std_ulogic";
        definition.element_subtype->domain = vhdl::ValueDomain::logic9;
        vhdl::ArrayDimension dimension;
        dimension.index_subtype = vhdl_name("natural", builder.source);
        dimension.unconstrained = true;
        dimension.source = builder.source;
        definition.array_dimensions.push_back(std::move(dimension));
        definition.source = builder.source;
        definition.origin = builder.origin;
        builder.vhdl_hir.mutable_types().push_back(
            std::move(definition));
        builder.vhdl_unit(numeric_std).declarations.push_back(declaration);
        return std::pair { declaration, type };
    };
    const auto [unsigned_array_decl, unsigned_array]
        = add_numeric_vector("UNRESOLVED_UNSIGNED");
    const auto [signed_array_decl, signed_array]
        = add_numeric_vector("UNRESOLVED_SIGNED");
    (void)unsigned_array_decl;
    (void)signed_array_decl;
    const auto add_numeric_subtype = [&](const std::string& name,
                                         const TypeId type,
                                         const bool signed_value) {
        const auto declaration = builder.model.add_declaration(
            numeric_std_scope, DeclarationKind::type, name,
            builder.source, builder.origin);
        vhdl::Declaration record;
        record.id = declaration;
        record.scope = numeric_std_scope;
        record.form = vhdl::DeclarationForm::subtype;
        record.name = name;
        record.source = builder.source;
        record.origin = builder.origin;
        record.subtype.emplace();
        record.subtype->type_mark.target = type;
        record.subtype->type_mark.spelling = name;
        record.subtype->domain = vhdl::ValueDomain::logic9;
        record.subtype->signed_value = signed_value;
        record.subtype->unconstrained = true;
        builder.vhdl_hir.mutable_declarations().push_back(
            std::move(record));
        builder.vhdl_unit(numeric_std).declarations.push_back(declaration);
        return declaration;
    };
    const auto unsigned_type = add_numeric_subtype(
        "UNSIGNED", unsigned_array, false);
    const auto signed_type = add_numeric_subtype(
        "SIGNED", signed_array, true);
    const auto add_to_integer_overload = [&](const TypeId argument_type,
                                             const bool signed_value) {
        const auto function_scope = builder.model.add_scope(
            numeric_std, numeric_std_scope,
            signed_value ? "to_integer_signed" : "to_integer_unsigned",
            builder.source, builder.origin);
        const auto formal = builder.model.add_declaration(
            function_scope, DeclarationKind::constant, "arg",
            builder.source, builder.origin);
        vhdl::Declaration formal_record;
        formal_record.id = formal;
        formal_record.scope = function_scope;
        formal_record.form = vhdl::DeclarationForm::constant;
        formal_record.name = "arg";
        formal_record.source = builder.source;
        formal_record.origin = builder.origin;
        formal_record.subtype.emplace();
        formal_record.subtype->type_mark.target = argument_type;
        formal_record.subtype->type_mark.spelling = signed_value
            ? "UNRESOLVED_SIGNED"
            : "UNRESOLVED_UNSIGNED";
        formal_record.subtype->domain = vhdl::ValueDomain::logic9;
        formal_record.subtype->signed_value = signed_value;
        formal_record.subtype->unconstrained = true;
        builder.vhdl_hir.mutable_declarations().push_back(
            std::move(formal_record));
        const auto function = builder.model.add_declaration(
            numeric_std_scope, DeclarationKind::function, "to_integer",
            builder.source, builder.origin);
        vhdl::Declaration function_record;
        function_record.id = function;
        function_record.scope = numeric_std_scope;
        function_record.form = vhdl::DeclarationForm::function;
        function_record.name = "to_integer";
        function_record.source = builder.source;
        function_record.origin = builder.origin;
        function_record.callable.emplace();
        function_record.callable->function = true;
        function_record.callable->pure = true;
        function_record.callable->defined = true;
        function_record.callable->formals = { formal };
        function_record.children = { formal };
        function_record.callable->return_type.emplace();
        function_record.callable->return_type->type_mark.spelling
            = signed_value ? "integer" : "natural";
        function_record.callable->return_type->domain
            = vhdl::ValueDomain::integer;
        builder.vhdl_hir.mutable_declarations().push_back(
            std::move(function_record));
        builder.vhdl_unit(numeric_std).declarations.push_back(function);
        return function;
    };
    const auto unsigned_to_integer = add_to_integer_overload(
        unsigned_array, false);
    const auto signed_to_integer = add_to_integer_overload(
        signed_array, true);
    vhdl::ContextItem numeric_std_import;
    numeric_std_import.kind = vhdl::ContextKind::use_clause;
    numeric_std_import.selected_names.push_back(
        vhdl_name("ieee.numeric_std.all", builder.source));
    builder.vhdl_unit(unit).context.push_back(std::move(numeric_std_import));
    const auto add_expression_in_scope
        = [&](const ScopeId expression_scope,
              const vhdl::ExpressionKind kind, std::string text,
              std::vector<ExpressionId> operands = { },
              const std::optional<DeclarationId> selected = std::nullopt) {
              const auto id = builder.model.add_expression_identity(
                  expression_scope, builder.source, builder.origin);
              vhdl::Expression expression;
              expression.id = id;
              expression.scope = expression_scope;
              expression.kind = kind;
              expression.text = std::move(text);
              expression.operands = std::move(operands);
              expression.source = builder.source;
              expression.origin = builder.origin;
              if (selected) {
                  expression.referenced_name.emplace();
                  expression.referenced_name->spelling = expression.text;
                  expression.referenced_name->canonical = expression.text;
                  expression.referenced_name->selected = selected;
              }
              builder.vhdl_hir.mutable_expressions().push_back(
                  std::move(expression));
              return id;
          };
    const auto add_expression
        = [&](const vhdl::ExpressionKind kind, std::string text,
              std::vector<ExpressionId> operands = { },
              const std::optional<DeclarationId> selected = std::nullopt) {
              return add_expression_in_scope(scope, kind,
                  std::move(text), std::move(operands), selected);
          };
    const auto literal = [&](const std::int64_t value) {
        return add_expression(vhdl::ExpressionKind::integer_literal,
            std::to_string(value));
    };
    const auto add_declaration
        = [&](const DeclarationKind kind,
              const vhdl::DeclarationForm form, std::string name,
              const std::optional<ExpressionId> initializer = std::nullopt) {
              const auto id = builder.model.add_declaration(
                  scope, kind, name, builder.source, builder.origin);
              vhdl::Declaration declaration;
              declaration.id = id;
              declaration.scope = scope;
              declaration.form = form;
              declaration.name = std::move(name);
              declaration.initializer = initializer;
              declaration.source = builder.source;
              declaration.origin = builder.origin;
              builder.vhdl_hir.mutable_declarations().push_back(
                  std::move(declaration));
              return id;
          };
    const auto name = [&](const std::string& spelling,
                          const DeclarationId declaration) {
        return add_expression(vhdl::ExpressionKind::name,
            spelling, { }, declaration);
    };
    const auto binary = [&](const std::string& operation,
                            const ExpressionId left,
                            const ExpressionId right) {
        return add_expression(vhdl::ExpressionKind::binary,
            operation, { left, right });
    };
    const auto function = add_declaration(DeclarationKind::function,
        vhdl::DeclarationForm::function, "gf_pow_u");
    const auto exponent = add_declaration(DeclarationKind::constant,
        vhdl::DeclarationForm::constant, "exp");
    const auto m = add_declaration(DeclarationKind::constant,
        vhdl::DeclarationForm::constant, "m");
    const auto poly = add_declaration(DeclarationKind::constant,
        vhdl::DeclarationForm::constant, "poly");
    const auto runtime = add_declaration(DeclarationKind::variable,
        vhdl::DeclarationForm::variable, "runtime");
    auto integer_subtype = [] {
        vhdl::SubtypeIndication subtype;
        subtype.type_mark.spelling = "integer";
        subtype.domain = vhdl::ValueDomain::integer;
        return subtype;
    };
    for (const auto formal : { exponent, m, poly }) {
        auto record = std::ranges::find(
            builder.vhdl_hir.mutable_declarations(), formal,
            &vhdl::Declaration::id);
        record->subtype = integer_subtype();
    }
    const auto m_name = name("m", m);
    const auto zero = literal(0);
    const auto one = literal(1);
    const auto m_plus_one = binary("+", m_name, one);
    const auto to_unsigned_one = add_expression(
        vhdl::ExpressionKind::call, "to_unsigned", { one, m_plus_one });
    const auto val = add_declaration(DeclarationKind::variable,
        vhdl::DeclarationForm::variable, "val", to_unsigned_one);
    vhdl::SubtypeIndication val_subtype;
    val_subtype.type_mark.spelling = "unsigned";
    val_subtype.domain = vhdl::ValueDomain::logic9;
    vhdl::RangeConstraint val_range;
    val_range.kind = vhdl::RangeKind::array_index;
    val_range.left_expression = m_name;
    val_range.right_expression = zero;
    val_range.descending = true;
    val_range.source = builder.source;
    val_subtype.constraints.push_back(val_range);
    auto val_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), val,
        &vhdl::Declaration::id);
    val_record->subtype = val_subtype;

    const auto two = literal(2);
    const auto exponent_power = binary("**", two, m_name);
    const auto order = binary("-", exponent_power, one);
    const auto exponent_name = name("exp", exponent);
    const auto reduced_exponent = binary("mod", exponent_name, order);
    const auto loop_index_scope = builder.model.add_scope(
        unit, scope, "gf_pow_u:loop", builder.source, builder.origin);
    const auto loop_index = builder.model.add_declaration(
        loop_index_scope, DeclarationKind::constant, "j",
        builder.source, builder.origin);
    vhdl::Declaration loop_index_record;
    loop_index_record.id = loop_index;
    loop_index_record.scope = loop_index_scope;
    loop_index_record.form = vhdl::DeclarationForm::constant;
    loop_index_record.name = "j";
    loop_index_record.source = builder.source;
    loop_index_record.origin = builder.origin;
    builder.vhdl_hir.mutable_declarations().push_back(
        std::move(loop_index_record));
    const auto loop = builder.model.add_statement_identity(
        scope, builder.source, builder.origin);
    vhdl::Statement loop_record;
    loop_record.id = loop;
    loop_record.scope = scope;
    loop_record.kind = vhdl::StatementKind::loop;
    loop_record.loop_variable = "j";
    loop_record.loop_initial = zero;
    loop_record.loop_limit = binary("-", reduced_exponent, one);
    loop_record.nested_scope = loop_index_scope;
    loop_record.declarations = { loop_index };
    loop_record.source = builder.source;
    loop_record.origin = builder.origin;
    const auto assign_val = builder.model.add_statement_identity(
        scope, builder.source, builder.origin);
    vhdl::Statement assign_val_record;
    assign_val_record.id = assign_val;
    assign_val_record.scope = scope;
    assign_val_record.kind = vhdl::StatementKind::variable_assignment;
    assign_val_record.target = name("val", val);
    const auto shifted = binary("sll", name("val", val), one);
    const auto poly_name = name("poly", poly);
    const auto poly_width = binary("+", name("m", m), one);
    const auto to_unsigned_poly = add_expression(
        vhdl::ExpressionKind::call, "to_unsigned",
        { poly_name, poly_width });
    assign_val_record.value = binary(
        "xor", shifted, to_unsigned_poly);
    assign_val_record.source = builder.source;
    assign_val_record.origin = builder.origin;
    loop_record.statements = { assign_val };
    builder.vhdl_hir.mutable_statements().push_back(
        std::move(assign_val_record));
    builder.vhdl_hir.mutable_statements().push_back(
        std::move(loop_record));
    const auto returned_slice = add_expression(
        vhdl::ExpressionKind::slice, "downto",
        { name("val", val), binary("-", name("m", m), one), zero });
    const auto return_statement = builder.model.add_statement_identity(
        scope, builder.source, builder.origin);
    vhdl::Statement return_record;
    return_record.id = return_statement;
    return_record.scope = scope;
    return_record.kind = vhdl::StatementKind::return_statement;
    return_record.value = returned_slice;
    return_record.source = builder.source;
    return_record.origin = builder.origin;
    builder.vhdl_hir.mutable_statements().push_back(
        std::move(return_record));

    auto function_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), function,
        &vhdl::Declaration::id);
    function_record->callable.emplace();
    function_record->callable->function = true;
    function_record->callable->pure = true;
    function_record->callable->defined = true;
    function_record->callable->formals = { exponent, m, poly };
    function_record->children = { exponent, m, poly, val };
    function_record->statements = { loop, return_statement };
    vhdl::SubtypeIndication return_subtype = val_subtype;
    return_subtype.constraints.front().left_expression
        = binary("-", name("m", m), one);
    function_record->callable->return_type = return_subtype;
    builder.vhdl_unit(unit).declarations.push_back(function);

    const auto static_call = add_expression(
        vhdl::ExpressionKind::call, "gf_pow_u",
        { literal(2), literal(8), literal(1) }, function);
    const auto set_to_integer_overloads = [&](const ExpressionId call) {
        auto record = std::ranges::find(
            builder.vhdl_hir.mutable_expressions(), call,
            &vhdl::Expression::id);
        assert(record != builder.vhdl_hir.mutable_expressions().end());
        record->referenced_name.emplace();
        record->referenced_name->spelling = "to_integer";
        record->referenced_name->canonical = "to_integer";
        record->referenced_name->overloads = {
            unsigned_to_integer, signed_to_integer
        };
    };
    const auto slice_constant_initializer = add_expression(
        vhdl::ExpressionKind::string_literal, "\"00000111\"");
    const auto slice_constant = add_declaration(
        DeclarationKind::constant, vhdl::DeclarationForm::constant,
        "slice_constant", slice_constant_initializer);
    auto slice_constant_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), slice_constant,
        &vhdl::Declaration::id);
    assert(slice_constant_record
        != builder.vhdl_hir.mutable_declarations().end());
    slice_constant_record->subtype.emplace();
    slice_constant_record->subtype->type_mark.target = unsigned_array;
    slice_constant_record->subtype->type_mark.spelling
        = "UNRESOLVED_UNSIGNED";
    slice_constant_record->subtype->domain = vhdl::ValueDomain::logic9;
    slice_constant_record->subtype->unconstrained = true;
    const auto unsigned_cast = add_expression(
        vhdl::ExpressionKind::call, "unsigned", { static_call },
        unsigned_type);
    const auto packed_function_to_integer = add_expression(
        vhdl::ExpressionKind::call, "to_integer", { unsigned_cast });
    set_to_integer_overloads(packed_function_to_integer);
    const auto sliced_unsigned = add_expression(
        vhdl::ExpressionKind::slice, "downto",
        { name("slice_constant", slice_constant), literal(2), zero });
    const auto sliced_to_integer_call = add_expression(
        vhdl::ExpressionKind::call, "to_integer", { sliced_unsigned });
    set_to_integer_overloads(sliced_to_integer_call);
    const auto out_of_range_unsigned = add_expression(
        vhdl::ExpressionKind::slice, "downto",
        { name("slice_constant", slice_constant), literal(8), zero });
    const auto out_of_range_to_integer_call = add_expression(
        vhdl::ExpressionKind::call, "to_integer",
        { out_of_range_unsigned });
    set_to_integer_overloads(out_of_range_to_integer_call);
    const auto signed_bits = add_expression(
        vhdl::ExpressionKind::string_literal, "\"1110\"");
    const auto signed_cast = add_expression(
        vhdl::ExpressionKind::call, "signed", { signed_bits }, signed_type);
    const auto signed_to_integer_call = add_expression(
        vhdl::ExpressionKind::call, "to_integer",
        { signed_cast });
    set_to_integer_overloads(signed_to_integer_call);
    const auto unknown_bits = add_expression(
        vhdl::ExpressionKind::string_literal, "\"10X1\"");
    const auto unknown_unsigned = add_expression(
        vhdl::ExpressionKind::call, "unsigned", { unknown_bits },
        unsigned_type);
    const auto unknown_to_integer = add_expression(
        vhdl::ExpressionKind::call, "to_integer", { unknown_unsigned });
    set_to_integer_overloads(unknown_to_integer);
    const auto untyped_bits = add_expression(
        vhdl::ExpressionKind::string_literal, "\"1010\"");
    const auto ambiguous_to_integer = add_expression(
        vhdl::ExpressionKind::call, "to_integer", { untyped_bits });
    set_to_integer_overloads(ambiguous_to_integer);
    const auto untyped_slice = add_expression(
        vhdl::ExpressionKind::slice, "downto",
        { untyped_bits, literal(3), zero });
    const auto untyped_slice_to_integer = add_expression(
        vhdl::ExpressionKind::call, "to_integer", { untyped_slice });
    set_to_integer_overloads(untyped_slice_to_integer);
    const auto oversized_bits = add_expression(
        vhdl::ExpressionKind::string_literal,
        "\"1" + std::string(32U, '0') + "\"");
    const auto oversized_unsigned = add_expression(
        vhdl::ExpressionKind::call, "unsigned", { oversized_bits },
        unsigned_type);
    const auto oversized_to_integer = add_expression(
        vhdl::ExpressionKind::call, "to_integer", { oversized_unsigned });
    set_to_integer_overloads(oversized_to_integer);
    const auto runtime_call = add_expression(
        vhdl::ExpressionKind::call, "gf_pow_u",
        { name("runtime", runtime), literal(8), literal(1) }, function);

    const auto nibble_type_declaration = add_declaration(
        DeclarationKind::type, vhdl::DeclarationForm::subtype,
        "nibble_t");
    TypeReference nibble_base;
    nibble_base.source = builder.source;
    nibble_base.spelling = "nibble_t";
    const auto nibble_type = builder.model.add_type(scope,
        TypeKind::declaration, "nibble_t", nibble_base,
        builder.source, builder.origin);
    auto nibble_declaration = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), nibble_type_declaration,
        &vhdl::Declaration::id);
    nibble_declaration->declared_type = nibble_type;
    vhdl::TypeDefinition nibble_definition;
    nibble_definition.id = nibble_type;
    nibble_definition.declaration = nibble_type_declaration;
    nibble_definition.form = vhdl::TypeForm::subtype;
    nibble_definition.name = "nibble_t";
    nibble_definition.base.type_mark.spelling = "std_logic_vector";
    nibble_definition.base.domain = vhdl::ValueDomain::logic9;
    vhdl::RangeConstraint nibble_range;
    nibble_range.kind = vhdl::RangeKind::array_index;
    nibble_range.left = 3;
    nibble_range.right = 0;
    nibble_range.descending = true;
    nibble_definition.base.constraints.push_back(nibble_range);
    nibble_definition.source = builder.source;
    nibble_definition.origin = builder.origin;
    builder.vhdl_hir.mutable_types().push_back(
        std::move(nibble_definition));
    builder.vhdl_unit(unit).declarations.push_back(
        nibble_type_declaration);

    const auto constant_packed = add_expression(
        vhdl::ExpressionKind::call, "to_unsigned",
        { literal(7), literal(8) });
    const auto constant_nibble = add_expression(
        vhdl::ExpressionKind::call, "to_unsigned",
        { literal(7), literal(4) });
    const auto add_constant_return_function
        = [&](std::string function_name,
              vhdl::SubtypeIndication return_subtype,
              const ExpressionId return_value) {
              const auto function_id = add_declaration(
                  DeclarationKind::function,
                  vhdl::DeclarationForm::function,
                  std::move(function_name));
              const auto statement_id = builder.model
                  .add_statement_identity(
                      scope, builder.source, builder.origin);
              vhdl::Statement statement;
              statement.id = statement_id;
              statement.scope = scope;
              statement.kind = vhdl::StatementKind::return_statement;
              statement.value = return_value;
              statement.source = builder.source;
              statement.origin = builder.origin;
              builder.vhdl_hir.mutable_statements().push_back(
                  std::move(statement));
              auto record = std::ranges::find(
                  builder.vhdl_hir.mutable_declarations(), function_id,
                  &vhdl::Declaration::id);
              record->callable.emplace();
              record->callable->function = true;
              record->callable->pure = true;
              record->callable->defined = true;
              record->callable->return_type = std::move(return_subtype);
              record->statements = { statement_id };
              builder.vhdl_unit(unit).declarations.push_back(function_id);
              return add_expression(vhdl::ExpressionKind::call,
                  record->name, { }, function_id);
          };
    const auto add_packed_formal_return_function
        = [&](std::string function_name,
              vhdl::SubtypeIndication return_subtype,
              const DeclarationId formal,
              const ExpressionId return_value,
              const ExpressionId actual) {
              const auto function_id = add_declaration(
                  DeclarationKind::function,
                  vhdl::DeclarationForm::function, function_name);
              const auto statement_id = builder.model
                  .add_statement_identity(
                      scope, builder.source, builder.origin);
              vhdl::Statement statement;
              statement.id = statement_id;
              statement.scope = scope;
              statement.kind = vhdl::StatementKind::return_statement;
              statement.value = return_value;
              statement.source = builder.source;
              statement.origin = builder.origin;
              builder.vhdl_hir.mutable_statements().push_back(
                  std::move(statement));
              auto record = std::ranges::find(
                  builder.vhdl_hir.mutable_declarations(), function_id,
                  &vhdl::Declaration::id);
              record->callable.emplace();
              record->callable->function = true;
              record->callable->pure = true;
              record->callable->defined = true;
              record->callable->formals = { formal };
              record->callable->return_type = std::move(return_subtype);
              record->children = { formal };
              record->statements = { statement_id };
              builder.vhdl_unit(unit).declarations.push_back(function_id);
              return add_expression(vhdl::ExpressionKind::call,
                  std::move(function_name), { actual }, function_id);
          };
    vhdl::SubtypeIndication nibble_return;
    nibble_return.type_mark.target = nibble_type;
    nibble_return.type_mark.spelling = "nibble_t";
    const auto alias_return_call = add_constant_return_function(
        "nibble_alias_result", nibble_return, constant_nibble);
    const auto alias_wrong_width_return_call = add_constant_return_function(
        "nibble_alias_wrong_width_result", nibble_return,
        constant_packed);

    vhdl::SubtypeIndication unconstrained_unsigned;
    unconstrained_unsigned.type_mark.spelling = "unsigned";
    unconstrained_unsigned.domain = vhdl::ValueDomain::logic9;
    unconstrained_unsigned.unconstrained = true;
    const auto unconstrained_return_call = add_constant_return_function(
        "unconstrained_unsigned_result", unconstrained_unsigned,
        constant_packed);
    auto unconstrained_unsigned_placeholder_width = unconstrained_unsigned;
    unconstrained_unsigned_placeholder_width.executable_width = 1U;
    const auto unconstrained_placeholder_width_return_call
        = add_constant_return_function(
            "unconstrained_unsigned_placeholder_width_result",
            unconstrained_unsigned_placeholder_width, constant_packed);
    auto constrained_one_bit_unsigned = unconstrained_unsigned;
    constrained_one_bit_unsigned.unconstrained = false;
    constrained_one_bit_unsigned.executable_width = 1U;
    const auto constrained_one_bit_return_call = add_constant_return_function(
        "constrained_one_bit_unsigned_result",
        constrained_one_bit_unsigned, constant_packed);

    const auto unconstrained_formal = add_declaration(
        DeclarationKind::constant, vhdl::DeclarationForm::constant,
        "unconstrained_formal");
    auto unconstrained_formal_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), unconstrained_formal,
        &vhdl::Declaration::id);
    unconstrained_formal_record->subtype
        = unconstrained_unsigned_placeholder_width;
    const auto unconstrained_formal_identity
        = add_packed_formal_return_function(
            "unconstrained_formal_identity",
            unconstrained_unsigned_placeholder_width,
            unconstrained_formal, name("unconstrained_formal",
                unconstrained_formal), constant_packed);

    const auto constrained_formal = add_declaration(
        DeclarationKind::constant, vhdl::DeclarationForm::constant,
        "constrained_formal");
    auto constrained_formal_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), constrained_formal,
        &vhdl::Declaration::id);
    constrained_formal_record->subtype = constrained_one_bit_unsigned;
    const auto constrained_formal_identity
        = add_packed_formal_return_function(
            "constrained_formal_identity", unconstrained_unsigned,
            constrained_formal, name("constrained_formal",
                constrained_formal), constant_packed);

    vhdl::SubtypeIndication integer_return;
    integer_return.type_mark.spelling = "integer";
    integer_return.domain = vhdl::ValueDomain::integer;
    const auto wrong_domain_return_call = add_constant_return_function(
        "packed_value_as_integer", integer_return, constant_packed);

    auto seven_bit_return = unconstrained_unsigned;
    seven_bit_return.unconstrained = false;
    vhdl::RangeConstraint seven_bit_range;
    seven_bit_range.kind = vhdl::RangeKind::array_index;
    seven_bit_range.left = 6;
    seven_bit_range.right = 0;
    seven_bit_range.descending = true;
    seven_bit_return.constraints.push_back(seven_bit_range);
    const auto wrong_width_return_call = add_constant_return_function(
        "wrong_width_unsigned_result", seven_bit_return,
        constant_packed);

    std::vector<CompiledDesign> inputs;
    inputs.push_back(builder.finish());
    auto linked = link_compiled_designs(std::move(inputs));
    assert(linked.ok());
    auto design = std::move(*linked.design);
    assert(design.valid());
    const auto numeric_std_imports = design.vhdl_linked_imports(unit);
    assert(numeric_std_imports);
    assert(std::ranges::any_of(*numeric_std_imports,
        [](const CompiledVhdlImport& imported) {
            return imported.library == "ieee"
                && imported.package == "numeric_std"
                && imported.member == "all";
        }));
    const auto conversion_call = design.find_expression(
        packed_function_to_integer);
    assert(conversion_call && conversion_call->vhdl != nullptr
        && conversion_call->vhdl->referenced_name
        && !conversion_call->vhdl->referenced_name->selected
        && conversion_call->vhdl->referenced_name->overloads.size() == 2U);
    const std::array<SpecializedHirActualIdentity, 0> actuals { };
    const auto specialized = make_specialized_hir_unit(
        design, unit, actuals);
    assert(specialized);
    const auto evaluated
        = specialized->evaluate_vhdl_constant_expression(static_call);
    assert(evaluated);
    const auto packed
        = std::get_if<SpecializedHirVhdlPackedValue>(&*evaluated);
    assert(packed);
    assert(packed->bits == "00000111");
    assert(packed->left_bound == 7);
    assert(packed->right_bound == 0);
    const auto packed_integer
        = specialized->evaluate_vhdl_constant_expression(
            packed_function_to_integer);
    assert(packed_integer);
    const auto packed_integer_value = std::get_if<std::int64_t>(
        &*packed_integer);
    assert(packed_integer_value && *packed_integer_value == 7);
    const auto sliced_integer
        = specialized->evaluate_vhdl_constant_expression(
            sliced_to_integer_call);
    assert(sliced_integer);
    const auto sliced_integer_value = std::get_if<std::int64_t>(
        &*sliced_integer);
    assert(sliced_integer_value && *sliced_integer_value == 7);
    const auto signed_integer
        = specialized->evaluate_vhdl_constant_expression(
            signed_to_integer_call);
    assert(signed_integer);
    const auto signed_integer_value = std::get_if<std::int64_t>(
        &*signed_integer);
    assert(signed_integer_value && *signed_integer_value == -2);
    assert(!specialized->evaluate_vhdl_constant_expression(
        unknown_to_integer));
    assert(!specialized->evaluate_vhdl_constant_expression(
        ambiguous_to_integer));
    assert(!specialized->evaluate_vhdl_constant_expression(
        untyped_slice_to_integer));
    assert(!specialized->evaluate_vhdl_constant_expression(
        out_of_range_to_integer_call));
    assert(!specialized->evaluate_vhdl_constant_expression(
        oversized_to_integer));
    const auto alias_result
        = specialized->evaluate_vhdl_constant_expression(alias_return_call);
    assert(alias_result);
    const auto alias_packed
        = std::get_if<SpecializedHirVhdlPackedValue>(&*alias_result);
    assert(alias_packed);
    assert(alias_packed->bits == "0111");
    assert(alias_packed->left_bound == 3);
    assert(alias_packed->right_bound == 0);
    assert(!specialized->evaluate_vhdl_constant_expression(
        alias_wrong_width_return_call));
    const auto unconstrained_result
        = specialized->evaluate_vhdl_constant_expression(
            unconstrained_return_call);
    assert(unconstrained_result);
    const auto unconstrained_packed
        = std::get_if<SpecializedHirVhdlPackedValue>(
            &*unconstrained_result);
    assert(unconstrained_packed);
    assert(unconstrained_packed->bits == "00000111");
    assert(unconstrained_packed->left_bound == 7);
    assert(unconstrained_packed->right_bound == 0);
    const auto placeholder_width_result
        = specialized->evaluate_vhdl_constant_expression(
            unconstrained_placeholder_width_return_call);
    assert(placeholder_width_result);
    const auto placeholder_width_packed
        = std::get_if<SpecializedHirVhdlPackedValue>(
            &*placeholder_width_result);
    assert(placeholder_width_packed);
    assert(placeholder_width_packed->bits == "00000111");
    assert(placeholder_width_packed->left_bound == 7);
    assert(placeholder_width_packed->right_bound == 0);
    assert(!specialized->evaluate_vhdl_constant_expression(
        constrained_one_bit_return_call));
    const auto unconstrained_formal_result
        = specialized->evaluate_vhdl_constant_expression(
            unconstrained_formal_identity);
    assert(unconstrained_formal_result);
    const auto unconstrained_formal_packed
        = std::get_if<SpecializedHirVhdlPackedValue>(
            &*unconstrained_formal_result);
    assert(unconstrained_formal_packed);
    assert(unconstrained_formal_packed->bits == "00000111");
    assert(unconstrained_formal_packed->left_bound == 7);
    assert(unconstrained_formal_packed->right_bound == 0);
    assert(!specialized->evaluate_vhdl_constant_expression(
        constrained_formal_identity));
    assert(!specialized->evaluate_vhdl_constant_expression(
        wrong_domain_return_call));
    assert(!specialized->evaluate_vhdl_constant_expression(
        wrong_width_return_call));
    assert(!specialized->evaluate_vhdl_constant_expression(runtime_call));
}

void test_specialized_hir_vhdl_generated_packed_constant_iterator()
{
    LinkBundleBuilder builder {
        "generated-packed-constant-iterator.vhd"
    };
    const auto unit = builder.add_vhdl_unit(
        UnitKind::vhdl_entity, vhdl::UnitKind::entity,
        "generated_packed_constant_iterator");
    const auto unit_scope = builder.model.units()[unit.value()].scope;
    const auto add_expression
        = [&](const ScopeId scope, const vhdl::ExpressionKind kind,
              std::string text, std::vector<ExpressionId> operands = { },
              const std::optional<DeclarationId> selected = std::nullopt) {
              const auto id = builder.model.add_expression_identity(
                  scope, builder.source, builder.origin);
              vhdl::Expression expression;
              expression.id = id;
              expression.scope = scope;
              expression.kind = kind;
              expression.text = std::move(text);
              expression.operands = std::move(operands);
              expression.source = builder.source;
              expression.origin = builder.origin;
              if (selected) {
                  expression.referenced_name.emplace();
                  expression.referenced_name->spelling = expression.text;
                  expression.referenced_name->canonical = expression.text;
                  expression.referenced_name->selected = selected;
              }
              builder.vhdl_hir.mutable_expressions().push_back(
                  std::move(expression));
              return id;
          };
    const auto literal = [&](const ScopeId scope, const std::int64_t value) {
        return add_expression(scope,
            vhdl::ExpressionKind::integer_literal,
            std::to_string(value));
    };
    const auto binary = [&](const ScopeId scope,
                            const std::string& operation,
                            const ExpressionId left,
                            const ExpressionId right) {
        return add_expression(scope, vhdl::ExpressionKind::binary,
            operation, { left, right });
    };
    const auto branch_scope = builder.model.add_scope(
        unit, unit_scope, "gen_syn", builder.source, builder.origin);
    const auto region_declaration = builder.model.add_declaration(
        unit_scope, DeclarationKind::generate, "gen_syn",
        builder.source, builder.origin);
    vhdl::Declaration region_record;
    region_record.id = region_declaration;
    region_record.scope = unit_scope;
    region_record.form = vhdl::DeclarationForm::generated;
    region_record.name = "gen_syn";
    region_record.source = builder.source;
    region_record.origin = builder.origin;
    builder.vhdl_hir.mutable_declarations().push_back(
        std::move(region_record));
    builder.vhdl_unit(unit).declarations.push_back(region_declaration);

    const auto iterator_name = add_expression(
        branch_scope, vhdl::ExpressionKind::name, "gi");
    const auto packed_initializer = add_expression(
        branch_scope, vhdl::ExpressionKind::call, "to_unsigned",
        { iterator_name, literal(branch_scope, 4) });
    const auto uninitialized_local = builder.model.add_declaration(
        branch_scope, DeclarationKind::variable, "uninitialized_local",
        builder.source, builder.origin);
    vhdl::Declaration local_record;
    local_record.id = uninitialized_local;
    local_record.scope = branch_scope;
    local_record.form = vhdl::DeclarationForm::variable;
    local_record.name = "uninitialized_local";
    local_record.source = builder.source;
    local_record.origin = builder.origin;
    builder.vhdl_hir.mutable_declarations().push_back(
        std::move(local_record));
    const auto local_name = add_expression(branch_scope,
        vhdl::ExpressionKind::name, "uninitialized_local", { },
        uninitialized_local);
    const auto shadowed_initializer = add_expression(
        branch_scope, vhdl::ExpressionKind::call, "to_unsigned",
        { local_name, literal(branch_scope, 4) });
    const auto add_packed_constant = [&](const std::string& name,
                                         const ExpressionId initializer,
                                         const std::int64_t left_bound) {
        const auto id = builder.model.add_declaration(
            branch_scope, DeclarationKind::constant, name,
            builder.source, builder.origin);
        vhdl::Declaration declaration;
        declaration.id = id;
        declaration.scope = branch_scope;
        declaration.form = vhdl::DeclarationForm::constant;
        declaration.name = name;
        declaration.initializer = initializer;
        vhdl::SubtypeIndication subtype;
        subtype.type_mark.spelling = "std_logic_vector";
        subtype.domain = vhdl::ValueDomain::logic9;
        vhdl::RangeConstraint range;
        range.kind = vhdl::RangeKind::array_index;
        range.left = left_bound;
        range.right = 0;
        range.descending = true;
        range.source = builder.source;
        subtype.constraints.push_back(range);
        declaration.subtype = std::move(subtype);
        declaration.source = builder.source;
        declaration.origin = builder.origin;
        builder.vhdl_hir.mutable_declarations().push_back(
            std::move(declaration));
        return id;
    };
    const auto alpha_i = add_packed_constant(
        "ALPHA_I", packed_initializer, 3);
    const auto mismatched_width = add_packed_constant(
        "MISMATCHED_WIDTH", packed_initializer, 2);
    const auto shadowed = add_packed_constant(
        "SHADOWED", shadowed_initializer, 3);

    vhdl::GenerateRegion generate;
    generate.declaration = region_declaration;
    generate.scope = branch_scope;
    generate.kind = vhdl::GenerateKind::iterative;
    generate.label = "gen_syn";
    generate.iterator = "gi";
    generate.initial = literal(unit_scope, 0);
    generate.condition = binary(branch_scope, "<=", iterator_name,
        literal(branch_scope, 0));
    generate.iteration = binary(branch_scope, "+", iterator_name,
        literal(branch_scope, 1));
    generate.declarations = { alpha_i, mismatched_width, shadowed };
    generate.source = builder.source;
    generate.origin = builder.origin;
    builder.vhdl_unit(unit).generates.push_back(std::move(generate));

    auto design = builder.finish();
    assert(design.valid());
    const auto specialized = make_specialized_hir_unit(
        design, unit,
        std::span<const SpecializedHirActualIdentity> { });
    assert(specialized);
    assert(!specialized->evaluate_vhdl_packed_value_declaration(alpha_i));

    const std::array<SpecializedHirNamedIdentity, 1> iterator_identity {
        SpecializedHirNamedIdentity { "GI", "5" }
    };
    const auto occurrence
        = specialized->with_hierarchy_identities(iterator_identity);
    const auto alpha_value
        = occurrence.evaluate_vhdl_packed_value_declaration(alpha_i);
    assert(alpha_value);
    assert(alpha_value->bits == "0101");
    assert(alpha_value->left_bound == 3);
    assert(alpha_value->right_bound == 0);
    assert(!occurrence.evaluate_vhdl_packed_value_declaration(
        mismatched_width));
    assert(!occurrence.evaluate_vhdl_packed_value_declaration(shadowed));

    const std::array<SpecializedHirNamedIdentity, 1> invalid_identity {
        SpecializedHirNamedIdentity { "gi", "not-static" }
    };
    assert(!specialized->with_hierarchy_identities(invalid_identity)
                .evaluate_vhdl_packed_value_declaration(alpha_i));
    const std::array<SpecializedHirNamedIdentity, 1> oversized_identity {
        SpecializedHirNamedIdentity { "gi", "16" }
    };
    assert(!specialized->with_hierarchy_identities(oversized_identity)
                .evaluate_vhdl_packed_value_declaration(alpha_i));
    const std::array<SpecializedHirNamedIdentity, 2> ambiguous_identity {
        SpecializedHirNamedIdentity { "gi", "5" },
        SpecializedHirNamedIdentity { "GI", "5" },
    };
    assert(!specialized->with_hierarchy_identities(ambiguous_identity)
                .evaluate_vhdl_packed_value_declaration(alpha_i));
}

void test_specialized_hir_vhdl_selection_constant_function()
{
    LinkBundleBuilder builder { "selection-constant-function.vhd" };
    const auto unit = builder.add_vhdl_unit(
        UnitKind::vhdl_entity, vhdl::UnitKind::entity,
        "selection_constant_function");
    const auto scope = builder.model.units()[unit.value()].scope;
    const auto add_expression
        = [&](const vhdl::ExpressionKind kind, std::string text,
              std::vector<ExpressionId> operands = { },
              const std::optional<DeclarationId> selected = std::nullopt) {
              const auto id = builder.model.add_expression_identity(
                  scope, builder.source, builder.origin);
              vhdl::Expression expression;
              expression.id = id;
              expression.scope = scope;
              expression.kind = kind;
              expression.text = std::move(text);
              expression.operands = std::move(operands);
              expression.source = builder.source;
              expression.origin = builder.origin;
              if (selected) {
                  expression.referenced_name.emplace();
                  expression.referenced_name->spelling = expression.text;
                  expression.referenced_name->canonical = expression.text;
                  expression.referenced_name->selected = selected;
              }
              builder.vhdl_hir.mutable_expressions().push_back(
                  std::move(expression));
              return id;
          };
    const auto literal = [&](const std::int64_t value) {
        return add_expression(vhdl::ExpressionKind::integer_literal,
            std::to_string(value));
    };
    const auto add_declaration
        = [&](const DeclarationKind kind,
              const vhdl::DeclarationForm form, std::string name) {
              const auto id = builder.model.add_declaration(
                  scope, kind, name, builder.source, builder.origin);
              vhdl::Declaration declaration;
              declaration.id = id;
              declaration.scope = scope;
              declaration.form = form;
              declaration.name = std::move(name);
              declaration.source = builder.source;
              declaration.origin = builder.origin;
              builder.vhdl_hir.mutable_declarations().push_back(
                  std::move(declaration));
              return id;
          };
    const auto name = [&](const std::string& spelling,
                          const DeclarationId declaration) {
        return add_expression(vhdl::ExpressionKind::name,
            spelling, { }, declaration);
    };
    const auto add_statement = [&](const vhdl::StatementKind kind) {
        const auto id = builder.model.add_statement_identity(
            scope, builder.source, builder.origin);
        vhdl::Statement statement;
        statement.id = id;
        statement.scope = scope;
        statement.kind = kind;
        statement.source = builder.source;
        statement.origin = builder.origin;
        builder.vhdl_hir.mutable_statements().push_back(
            std::move(statement));
        return id;
    };
    const auto return_statement = [&](const std::int64_t value) {
        const auto id = add_statement(vhdl::StatementKind::return_statement);
        auto record = std::ranges::find(
            builder.vhdl_hir.mutable_statements(), id,
            &vhdl::Statement::id);
        record->value = literal(value);
        return id;
    };
    const auto add_function =
        [&](std::string function_name,
            std::vector<vhdl::CaseAlternative> alternatives,
            const std::int64_t actual) {
            const auto function = add_declaration(
                DeclarationKind::function, vhdl::DeclarationForm::function,
                function_name);
            const auto formal = add_declaration(
                DeclarationKind::constant, vhdl::DeclarationForm::constant,
                function_name + "_selector");
            const auto selection = add_statement(
                vhdl::StatementKind::selection);
            auto selection_record = std::ranges::find(
                builder.vhdl_hir.mutable_statements(), selection,
                &vhdl::Statement::id);
            selection_record->condition = name(
                function_name + "_selector", formal);
            selection_record->alternatives = std::move(alternatives);
            auto function_record = std::ranges::find(
                builder.vhdl_hir.mutable_declarations(), function,
                &vhdl::Declaration::id);
            function_record->callable.emplace();
            function_record->callable->function = true;
            function_record->callable->pure = true;
            function_record->callable->defined = true;
            function_record->callable->formals = { formal };
            function_record->children = { formal };
            function_record->statements = { selection };
            builder.vhdl_unit(unit).declarations.push_back(function);
            return add_expression(vhdl::ExpressionKind::call,
                std::move(function_name), { literal(actual) }, function);
        };
    const auto matching_alternative =
        [&](const std::int64_t choice, const std::int64_t result) {
            vhdl::CaseAlternative alternative;
            alternative.choices = { literal(choice) };
            alternative.statements = { return_statement(result) };
            alternative.source = builder.source;
            return alternative;
        };
    const auto default_alternative = [&](const std::int64_t result) {
        vhdl::CaseAlternative alternative;
        alternative.is_default = true;
        alternative.statements = { return_statement(result) };
        alternative.source = builder.source;
        return alternative;
    };

    const auto runtime = add_declaration(
        DeclarationKind::variable, vhdl::DeclarationForm::variable,
        "runtime_choice");
    const auto branch_call = add_function("select_branch", {
        matching_alternative(1, 11),
        matching_alternative(3, 33),
        default_alternative(99) }, 1);
    const auto second_branch_call = add_function("select_second_branch", {
        matching_alternative(1, 11),
        matching_alternative(3, 33),
        default_alternative(99) }, 3);
    const auto default_call = add_function("select_default", {
        matching_alternative(1, 11),
        default_alternative(99) }, 2);
    const auto duplicate_match_call = add_function("duplicate_match", {
        matching_alternative(1, 11),
        matching_alternative(1, 33) }, 1);
    const auto duplicate_default_call = add_function("duplicate_default", {
        default_alternative(11), default_alternative(33) }, 1);
    const auto unmatched_call = add_function("unmatched_selection", {
        matching_alternative(1, 11) }, 2);
    vhdl::CaseAlternative unresolved_choice;
    unresolved_choice.choices = { name("runtime_choice", runtime) };
    unresolved_choice.statements = { return_statement(11) };
    unresolved_choice.source = builder.source;
    const auto unresolved_call = add_function(
        "unresolved_choice", { std::move(unresolved_choice) }, 1);

    auto design = builder.finish();
    assert(design.valid());
    const std::array<SpecializedHirActualIdentity, 0> actuals { };
    const auto specialized = make_specialized_hir_unit(
        design, unit, actuals);
    assert(specialized);
    const auto evaluate_integer = [&](const ExpressionId expression,
                                      const std::int64_t expected) {
        const auto value
            = specialized->evaluate_vhdl_constant_expression(expression);
        assert(value);
        const auto integer = std::get_if<std::int64_t>(&*value);
        assert(integer && *integer == expected);
    };
    evaluate_integer(branch_call, 11);
    evaluate_integer(second_branch_call, 33);
    evaluate_integer(default_call, 99);
    assert(!specialized->evaluate_vhdl_constant_expression(
        duplicate_match_call));
    assert(!specialized->evaluate_vhdl_constant_expression(
        duplicate_default_call));
    assert(!specialized->evaluate_vhdl_constant_expression(unmatched_call));
    assert(!specialized->evaluate_vhdl_constant_expression(unresolved_call));
}

void test_specialized_hir_vhdl_local_array_constant_function()
{
    LinkBundleBuilder builder { "packed-array-constant-function.vhd" };
    const auto unit = builder.add_vhdl_unit(
        UnitKind::vhdl_entity, vhdl::UnitKind::entity,
        "packed_array_constant_function");
    const auto scope = builder.model.units()[unit.value()].scope;
    const auto add_expression_in_scope
        = [&](const ScopeId expression_scope,
              const vhdl::ExpressionKind kind, std::string text,
              std::vector<ExpressionId> operands = { },
              const std::optional<DeclarationId> selected = std::nullopt) {
              const auto id = builder.model.add_expression_identity(
                  expression_scope, builder.source, builder.origin);
              vhdl::Expression expression;
              expression.id = id;
              expression.scope = expression_scope;
              expression.kind = kind;
              expression.text = std::move(text);
              expression.operands = std::move(operands);
              expression.source = builder.source;
              expression.origin = builder.origin;
              if (selected) {
                  expression.referenced_name.emplace();
                  expression.referenced_name->spelling
                      = expression.text;
                  expression.referenced_name->canonical
                      = expression.text;
                  expression.referenced_name->selected = selected;
              }
              builder.vhdl_hir.mutable_expressions().push_back(
                  std::move(expression));
              return id;
          };
    const auto add_expression
        = [&](const vhdl::ExpressionKind kind, std::string text,
              std::vector<ExpressionId> operands = { },
              const std::optional<DeclarationId> selected = std::nullopt) {
              return add_expression_in_scope(scope, kind,
                  std::move(text), std::move(operands), selected);
          };
    const auto add_declaration
        = [&](const ScopeId declaration_scope,
              const DeclarationKind kind,
              const vhdl::DeclarationForm form, std::string name) {
              const auto id = builder.model.add_declaration(
                  declaration_scope, kind, name,
                  builder.source, builder.origin);
              vhdl::Declaration declaration;
              declaration.id = id;
              declaration.scope = declaration_scope;
              declaration.form = form;
              declaration.name = std::move(name);
              declaration.source = builder.source;
              declaration.origin = builder.origin;
              builder.vhdl_hir.mutable_declarations().push_back(
                  std::move(declaration));
              return id;
          };
    const auto literal = [&](const std::int64_t value) {
        return add_expression(vhdl::ExpressionKind::integer_literal,
            std::to_string(value));
    };
    const auto name = [&](const std::string& spelling,
                          const DeclarationId declaration) {
        return add_expression(vhdl::ExpressionKind::name,
            spelling, { }, declaration);
    };
    const auto binary = [&](const std::string& operation,
                            const ExpressionId left,
                            const ExpressionId right) {
        return add_expression(vhdl::ExpressionKind::binary,
            operation, { left, right });
    };
    const auto integer_subtype = [] {
        vhdl::SubtypeIndication subtype;
        subtype.type_mark.spelling = "integer";
        subtype.domain = vhdl::ValueDomain::integer;
        return subtype;
    };
    const auto vector_subtype = [&](const ExpressionId left,
                                    const ExpressionId right) {
        vhdl::SubtypeIndication subtype;
        subtype.type_mark.spelling = "std_logic_vector";
        subtype.domain = vhdl::ValueDomain::logic9;
        vhdl::RangeConstraint range;
        range.kind = vhdl::RangeKind::array_index;
        range.left_expression = left;
        range.right_expression = right;
        range.descending = true;
        range.source = builder.source;
        subtype.constraints.push_back(std::move(range));
        return subtype;
    };
    const auto make_assignment = [&](const ExpressionId target,
                                     const ExpressionId value) {
        const auto id = builder.model.add_statement_identity(
            scope, builder.source, builder.origin);
        vhdl::Statement statement;
        statement.id = id;
        statement.scope = scope;
        statement.kind = vhdl::StatementKind::variable_assignment;
        statement.target = target;
        statement.value = value;
        statement.source = builder.source;
        statement.origin = builder.origin;
        builder.vhdl_hir.mutable_statements().push_back(
            std::move(statement));
        return id;
    };

    const auto helper = add_declaration(scope,
        DeclarationKind::function, vhdl::DeclarationForm::function,
        "make_cell");
    const auto helper_value = add_declaration(scope,
        DeclarationKind::constant, vhdl::DeclarationForm::constant,
        "value");
    const auto helper_m = add_declaration(scope,
        DeclarationKind::constant, vhdl::DeclarationForm::constant,
        "width_value");
    for (const auto formal : { helper_value, helper_m }) {
        const auto record = std::ranges::find(
            builder.vhdl_hir.mutable_declarations(), formal,
            &vhdl::Declaration::id);
        record->subtype = integer_subtype();
    }
    const auto helper_width = name("width_value", helper_m);
    const auto helper_result = add_expression(
        vhdl::ExpressionKind::call, "to_unsigned",
        { literal(255), helper_width });
    const auto helper_return = builder.model.add_statement_identity(
        scope, builder.source, builder.origin);
    vhdl::Statement helper_return_record;
    helper_return_record.id = helper_return;
    helper_return_record.scope = scope;
    helper_return_record.kind = vhdl::StatementKind::return_statement;
    helper_return_record.value = helper_result;
    helper_return_record.source = builder.source;
    helper_return_record.origin = builder.origin;
    builder.vhdl_hir.mutable_statements().push_back(
        std::move(helper_return_record));
    auto helper_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), helper,
        &vhdl::Declaration::id);
    helper_record->callable.emplace();
    helper_record->callable->function = true;
    helper_record->callable->pure = true;
    helper_record->callable->defined = true;
    helper_record->callable->formals = { helper_value, helper_m };
    helper_record->callable->return_type = vector_subtype(
        binary("-", helper_width, literal(1)), literal(0));
    helper_record->children = { helper_value, helper_m };
    helper_record->statements = { helper_return };
    builder.vhdl_unit(unit).declarations.push_back(helper);

    const auto function = add_declaration(scope,
        DeclarationKind::function, vhdl::DeclarationForm::function,
        "pack_cells");
    const auto m = add_declaration(scope, DeclarationKind::constant,
        vhdl::DeclarationForm::constant, "M");
    auto m_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), m,
        &vhdl::Declaration::id);
    m_record->subtype = integer_subtype();
    const auto m_name = name("M", m);
    const auto zero = literal(0);
    const auto one = literal(1);
    const auto m_minus_one = binary("-", m_name, one);
    const auto cells_type_declaration = add_declaration(scope,
        DeclarationKind::type, vhdl::DeclarationForm::type,
        "cell_array_t");
    TypeReference type_base;
    type_base.source = builder.source;
    type_base.spelling = "cell_array_t";
    const auto cells_type = builder.model.add_type(scope,
        TypeKind::declaration, "cell_array_t", type_base,
        builder.source, builder.origin);
    auto type_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(),
        cells_type_declaration, &vhdl::Declaration::id);
    type_record->declared_type = cells_type;
    vhdl::TypeDefinition cells_definition;
    cells_definition.id = cells_type;
    cells_definition.declaration = cells_type_declaration;
    cells_definition.form = vhdl::TypeForm::array;
    cells_definition.name = "cell_array_t";
    cells_definition.element_subtype = vector_subtype(m_minus_one, zero);
    vhdl::ArrayDimension cells_dimension;
    cells_dimension.index_subtype.spelling = "integer";
    cells_dimension.constraint.emplace();
    cells_dimension.constraint->kind = vhdl::RangeKind::array_index;
    cells_dimension.constraint->left = 0;
    cells_dimension.constraint->right_expression = m_name;
    cells_dimension.constraint->source = builder.source;
    cells_dimension.source = builder.source;
    cells_definition.array_dimensions.push_back(
        std::move(cells_dimension));
    cells_definition.source = builder.source;
    cells_definition.origin = builder.origin;
    builder.vhdl_hir.mutable_types().push_back(
        std::move(cells_definition));

    const auto cells = add_declaration(scope,
        DeclarationKind::variable, vhdl::DeclarationForm::variable,
        "cells");
    auto cells_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), cells,
        &vhdl::Declaration::id);
    cells_record->declared_type = cells_type;
    const auto total_bits = binary("*", binary("+", m_name, one), m_name);
    const auto result_left = binary("-", total_bits, one);
    const auto result_subtype = vector_subtype(result_left, zero);
    const auto result_initializer = add_expression(
        vhdl::ExpressionKind::aggregate, "(...)");
    const auto others = add_expression(
        vhdl::ExpressionKind::default_choice, "others");
    vhdl::AggregateAssociation result_fill;
    result_fill.choices.push_back(others);
    result_fill.value = add_expression(
        vhdl::ExpressionKind::logic_literal, "'0'");
    result_fill.source = builder.source;
    auto result_initializer_record = std::ranges::find(
        builder.vhdl_hir.mutable_expressions(), result_initializer,
        &vhdl::Expression::id);
    result_initializer_record->associations.push_back(
        std::move(result_fill));
    const auto result = add_declaration(scope,
        DeclarationKind::variable, vhdl::DeclarationForm::variable,
        "result");
    auto result_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), result,
        &vhdl::Declaration::id);
    result_record->subtype = result_subtype;
    result_record->initializer = result_initializer;

    const auto loop_scope = builder.model.add_scope(
        unit, scope, "pack_cells:loop", builder.source, builder.origin);
    const auto loop_index = builder.model.add_declaration(
        loop_scope, DeclarationKind::constant, "j",
        builder.source, builder.origin);
    vhdl::Declaration loop_index_record;
    loop_index_record.id = loop_index;
    loop_index_record.scope = loop_scope;
    loop_index_record.form = vhdl::DeclarationForm::constant;
    loop_index_record.name = "j";
    loop_index_record.source = builder.source;
    loop_index_record.origin = builder.origin;
    builder.vhdl_hir.mutable_declarations().push_back(
        std::move(loop_index_record));
    const auto loop_index_name = add_expression_in_scope(loop_scope,
        vhdl::ExpressionKind::name, "j", { }, loop_index);
    const auto cells_name = name("cells", cells);
    const auto loop_target = add_expression(
        vhdl::ExpressionKind::index, "index",
        { cells_name, loop_index_name });
    const auto zero_cell = add_expression(
        vhdl::ExpressionKind::call, "to_unsigned", { zero, m_name });
    const auto loop_assignment = make_assignment(loop_target, zero_cell);
    const auto loop = builder.model.add_statement_identity(
        scope, builder.source, builder.origin);
    vhdl::Statement loop_record;
    loop_record.id = loop;
    loop_record.scope = scope;
    loop_record.kind = vhdl::StatementKind::loop;
    loop_record.loop_variable = "j";
    loop_record.loop_initial = zero;
    loop_record.loop_limit = m_name;
    loop_record.nested_scope = loop_scope;
    loop_record.declarations = { loop_index };
    loop_record.statements = { loop_assignment };
    loop_record.source = builder.source;
    loop_record.origin = builder.origin;
    builder.vhdl_hir.mutable_statements().push_back(
        std::move(loop_record));

    const auto cells_zero = add_expression(
        vhdl::ExpressionKind::index, "index", { cells_name, zero });
    const auto cell_sixty = add_expression(
        vhdl::ExpressionKind::call, "to_unsigned",
        { literal(60), m_name });
    const auto assign_zero = make_assignment(cells_zero, cell_sixty);
    const auto cells_m = add_expression(
        vhdl::ExpressionKind::index, "index", { cells_name, m_name });
    const auto make_cell = add_expression(
        vhdl::ExpressionKind::call, "make_cell",
        { zero, m_name }, helper);
    const auto assign_m = make_assignment(cells_m, make_cell);

    const auto cells_m_read = add_expression(
        vhdl::ExpressionKind::index, "index", { cells_name, m_name });
    const auto cast_cells_m = add_expression(
        vhdl::ExpressionKind::call, "std_logic_vector", { cells_m_read });
    const auto high_left = binary("-", total_bits, one);
    const auto high_right = binary("*", m_name, m_name);
    const auto high_target = add_expression(
        vhdl::ExpressionKind::slice, "downto",
        { name("result", result), high_left, high_right });
    const auto assign_high = make_assignment(high_target, cast_cells_m);

    const auto cells_zero_read = add_expression(
        vhdl::ExpressionKind::index, "index", { cells_name, zero });
    const auto cast_cells_zero = add_expression(
        vhdl::ExpressionKind::call, "std_logic_vector",
        { cells_zero_read });
    const auto low_target = add_expression(
        vhdl::ExpressionKind::slice, "downto",
        { name("result", result), m_minus_one, zero });
    const auto assign_low = make_assignment(low_target, cast_cells_zero);

    const auto return_statement = builder.model.add_statement_identity(
        scope, builder.source, builder.origin);
    vhdl::Statement return_record;
    return_record.id = return_statement;
    return_record.scope = scope;
    return_record.kind = vhdl::StatementKind::return_statement;
    return_record.value = name("result", result);
    return_record.source = builder.source;
    return_record.origin = builder.origin;
    builder.vhdl_hir.mutable_statements().push_back(
        std::move(return_record));

    const auto function_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), function,
        &vhdl::Declaration::id);
    function_record->callable.emplace();
    function_record->callable->function = true;
    function_record->callable->pure = true;
    function_record->callable->defined = true;
    function_record->callable->formals = { m };
    function_record->callable->return_type = result_subtype;
    function_record->children = {
        m, cells_type_declaration, cells, result
    };
    function_record->statements = {
        loop, assign_zero, assign_m, assign_high, assign_low,
        return_statement
    };
    builder.vhdl_unit(unit).declarations.push_back(function);

    const auto runtime = add_declaration(scope,
        DeclarationKind::variable, vhdl::DeclarationForm::variable,
        "runtime");
    const auto static_call = add_expression(
        vhdl::ExpressionKind::call, "pack_cells", { literal(8) }, function);
    const auto runtime_call = add_expression(
        vhdl::ExpressionKind::call, "pack_cells",
        { name("runtime", runtime) }, function);
    auto design = builder.finish();
    assert(design.valid());
    const std::array<SpecializedHirActualIdentity, 0> actuals { };
    const auto specialized = make_specialized_hir_unit(
        design, unit, actuals);
    assert(specialized);
    const auto evaluated
        = specialized->evaluate_vhdl_constant_expression(static_call);
    assert(evaluated);
    const auto packed
        = std::get_if<SpecializedHirVhdlPackedValue>(&*evaluated);
    assert(packed);
    assert(packed->bits
        == "11111111" + std::string(56U, '0') + "00111100");
    assert(packed->bits.size() == 72U);
    assert(packed->left_bound == 71);
    assert(packed->right_bound == 0);
    assert(!specialized->evaluate_vhdl_constant_expression(runtime_call));
}

void test_specialized_hir_vhdl_integer_array_constant_function()
{
    LinkBundleBuilder builder { "integer-array-constant-function.vhd" };
    const auto unit = builder.add_vhdl_unit(
        UnitKind::vhdl_entity, vhdl::UnitKind::entity,
        "integer_array_constant_function");
    const auto scope = builder.model.units()[unit.value()].scope;
    const auto add_expression = [&](const ScopeId expression_scope,
                                    const vhdl::ExpressionKind kind,
                                    std::string text,
                                    std::vector<ExpressionId> operands = { },
                                    const std::optional<DeclarationId>
                                        selected = std::nullopt) {
        const auto id = builder.model.add_expression_identity(
            expression_scope, builder.source, builder.origin);
        vhdl::Expression expression;
        expression.id = id;
        expression.scope = expression_scope;
        expression.kind = kind;
        expression.text = std::move(text);
        expression.operands = std::move(operands);
        expression.source = builder.source;
        expression.origin = builder.origin;
        if (selected) {
            expression.referenced_name.emplace();
            expression.referenced_name->spelling = expression.text;
            expression.referenced_name->canonical = expression.text;
            expression.referenced_name->selected = selected;
        }
        builder.vhdl_hir.mutable_expressions().push_back(
            std::move(expression));
        return id;
    };
    const auto add_declaration = [&](const ScopeId declaration_scope,
                                         const DeclarationKind kind,
                                         const vhdl::DeclarationForm form,
                                         std::string name) {
        const auto id = builder.model.add_declaration(
            declaration_scope, kind, name,
            builder.source, builder.origin);
        vhdl::Declaration declaration;
        declaration.id = id;
        declaration.scope = declaration_scope;
        declaration.form = form;
        declaration.name = std::move(name);
        declaration.source = builder.source;
        declaration.origin = builder.origin;
        builder.vhdl_hir.mutable_declarations().push_back(
            std::move(declaration));
        return id;
    };
    const auto integer_subtype = [] {
        vhdl::SubtypeIndication subtype;
        subtype.type_mark.spelling = "integer";
        subtype.domain = vhdl::ValueDomain::integer;
        return subtype;
    };
    const auto vector_subtype = [&](const std::int64_t left,
                                    const std::int64_t right) {
        vhdl::SubtypeIndication subtype;
        subtype.type_mark.spelling = "std_logic_vector";
        subtype.domain = vhdl::ValueDomain::logic9;
        vhdl::RangeConstraint range;
        range.kind = vhdl::RangeKind::array_index;
        range.left = left;
        range.right = right;
        range.descending = left >= right;
        range.source = builder.source;
        subtype.constraints.push_back(std::move(range));
        return subtype;
    };
    const auto expression = [&](const vhdl::ExpressionKind kind,
                                std::string text,
                                std::vector<ExpressionId> operands = { },
                                const std::optional<DeclarationId> selected
                                    = std::nullopt) {
        return add_expression(scope, kind, std::move(text),
            std::move(operands), selected);
    };
    const auto literal = [&](const std::int64_t value) {
        return expression(vhdl::ExpressionKind::integer_literal,
            std::to_string(value));
    };
    const auto name = [&](const ScopeId expression_scope,
                          const std::string& spelling,
                          const DeclarationId selected) {
        return add_expression(expression_scope,
            vhdl::ExpressionKind::name, spelling, { }, selected);
    };
    const auto binary = [&](const std::string& operation,
                            const ExpressionId left,
                            const ExpressionId right) {
        return expression(vhdl::ExpressionKind::binary,
            operation, { left, right });
    };
    const auto add_statement = [&](const ScopeId statement_scope,
                                   const vhdl::StatementKind kind) {
        const auto id = builder.model.add_statement_identity(
            statement_scope, builder.source, builder.origin);
        vhdl::Statement statement;
        statement.id = id;
        statement.scope = statement_scope;
        statement.kind = kind;
        statement.source = builder.source;
        statement.origin = builder.origin;
        builder.vhdl_hir.mutable_statements().push_back(
            std::move(statement));
        return id;
    };
    const auto add_assignment = [&](const ExpressionId target,
                                    const ExpressionId value) {
        const auto id = add_statement(
            scope, vhdl::StatementKind::variable_assignment);
        auto statement = std::ranges::find(
            builder.vhdl_hir.mutable_statements(), id,
            &vhdl::Statement::id);
        statement->target = target;
        statement->value = value;
        return id;
    };
    const auto add_integer_array_type = [&](const std::string& type_name,
                                                const bool two_dimensions,
                                                const vhdl::ValueDomain
                                                    element_domain) {
        const auto declaration = add_declaration(scope,
            DeclarationKind::type, vhdl::DeclarationForm::type,
            type_name);
        TypeReference base;
        base.source = builder.source;
        base.spelling = type_name;
        const auto type = builder.model.add_type(scope,
            TypeKind::declaration, type_name, base,
            builder.source, builder.origin);
        auto type_declaration = std::ranges::find(
            builder.vhdl_hir.mutable_declarations(), declaration,
            &vhdl::Declaration::id);
        type_declaration->declared_type = type;
        vhdl::TypeDefinition definition;
        definition.id = type;
        definition.declaration = declaration;
        definition.form = vhdl::TypeForm::array;
        definition.name = type_name;
        definition.element_subtype.emplace();
        definition.element_subtype->type_mark.spelling
            = element_domain == vhdl::ValueDomain::integer
            ? "integer"
            : "boolean";
        definition.element_subtype->domain = element_domain;
        if (element_domain == vhdl::ValueDomain::integer) {
            definition.element_subtype->executable_width = 32U;
            definition.element_subtype->signed_value = true;
            definition.element_subtype->integer_storage_width = 32U;
        }
        const auto dimension_count = two_dimensions ? 2U : 1U;
        for (std::size_t dimension_index { };
             dimension_index < dimension_count; ++dimension_index) {
            vhdl::ArrayDimension dimension;
            dimension.index_subtype.spelling = "natural";
            dimension.constraint.emplace();
            dimension.constraint->kind = vhdl::RangeKind::array_index;
            dimension.constraint->left = 0;
            dimension.constraint->right = 1;
            dimension.constraint->source = builder.source;
            dimension.source = builder.source;
            definition.array_dimensions.push_back(std::move(dimension));
        }
        definition.source = builder.source;
        definition.origin = builder.origin;
        builder.vhdl_hir.mutable_types().push_back(
            std::move(definition));
        return std::pair { declaration, type };
    };
    const auto add_others_initializer = [&](const ScopeId initializer_scope,
                                            const ExpressionId fill) {
        const auto aggregate = add_expression(initializer_scope,
            vhdl::ExpressionKind::aggregate, "(...)");
        const auto others = add_expression(initializer_scope,
            vhdl::ExpressionKind::default_choice, "others");
        vhdl::AggregateAssociation association;
        association.choices.push_back(others);
        association.value = fill;
        association.source = builder.source;
        const auto record = std::ranges::find(
            builder.vhdl_hir.mutable_expressions(), aggregate,
            &vhdl::Expression::id);
        record->associations.push_back(std::move(association));
        return aggregate;
    };

    const auto [array_type_declaration, array_type]
        = add_integer_array_type(
            "integer_array_t", false, vhdl::ValueDomain::integer);
    const auto array_type_record = std::ranges::find(
        builder.vhdl_hir.mutable_types(), array_type,
        &vhdl::TypeDefinition::id);
    array_type_record->array_dimensions.front().constraint->right = 512;

    const auto function = add_declaration(scope,
        DeclarationKind::function, vhdl::DeclarationForm::function,
        "fill_integer_array");
    const auto count = add_declaration(scope,
        DeclarationKind::constant, vhdl::DeclarationForm::constant,
        "integer_array_count");
    auto count_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), count,
        &vhdl::Declaration::id);
    count_record->subtype = integer_subtype();
    const auto mode = add_declaration(scope,
        DeclarationKind::constant, vhdl::DeclarationForm::constant,
        "integer_array_mode");
    auto mode_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), mode,
        &vhdl::Declaration::id);
    mode_record->subtype = integer_subtype();
    const auto values = add_declaration(scope,
        DeclarationKind::variable, vhdl::DeclarationForm::variable,
        "integer_array_values");
    auto values_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), values,
        &vhdl::Declaration::id);
    values_record->declared_type = array_type;
    values_record->initializer = add_others_initializer(scope, literal(0));

    const auto runtime_value = add_declaration(scope,
        DeclarationKind::variable, vhdl::DeclarationForm::variable,
        "integer_array_runtime_value");
    auto runtime_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), runtime_value,
        &vhdl::Declaration::id);
    runtime_record->subtype = integer_subtype();

    const auto count_name = name(scope, "integer_array_count", count);
    const auto mode_name = name(scope, "integer_array_mode", mode);
    const auto values_name = name(scope, "integer_array_values", values);
    const auto loop_scope = builder.model.add_scope(unit, scope,
        "fill_integer_array:loop", builder.source, builder.origin);
    const auto loop_index = builder.model.add_declaration(
        loop_scope, DeclarationKind::constant, "integer_array_i",
        builder.source, builder.origin);
    vhdl::Declaration loop_index_record;
    loop_index_record.id = loop_index;
    loop_index_record.scope = loop_scope;
    loop_index_record.form = vhdl::DeclarationForm::constant;
    loop_index_record.name = "integer_array_i";
    loop_index_record.source = builder.source;
    loop_index_record.origin = builder.origin;
    builder.vhdl_hir.mutable_declarations().push_back(
        std::move(loop_index_record));
    const auto loop_index_name = name(
        loop_scope, "integer_array_i", loop_index);
    const auto loop_target = expression(
        vhdl::ExpressionKind::index, "index",
        { values_name, loop_index_name });
    const auto loop_assignment = add_assignment(
        loop_target, loop_index_name);
    const auto loop = add_statement(scope, vhdl::StatementKind::loop);
    auto loop_record = std::ranges::find(
        builder.vhdl_hir.mutable_statements(), loop,
        &vhdl::Statement::id);
    loop_record->loop_variable = "integer_array_i";
    loop_record->loop_initial = literal(0);
    loop_record->loop_limit = count_name;
    loop_record->nested_scope = loop_scope;
    loop_record->declarations = { loop_index };
    loop_record->statements = { loop_assignment };

    const auto one = literal(1);
    const auto out_of_range_target = expression(
        vhdl::ExpressionKind::index, "index",
        { values_name, binary("+", count_name, one) });
    const auto out_of_range_assignment = add_assignment(
        out_of_range_target, one);
    const auto unknown_target = expression(
        vhdl::ExpressionKind::index, "index",
        { values_name, literal(0) });
    const auto unknown_assignment = add_assignment(
        unknown_target,
        name(scope, "integer_array_runtime_value", runtime_value));

    const auto mode_is_unknown = add_statement(
        scope, vhdl::StatementKind::conditional);
    auto unknown_branch = std::ranges::find(
        builder.vhdl_hir.mutable_statements(), mode_is_unknown,
        &vhdl::Statement::id);
    unknown_branch->condition = binary("=", mode_name, literal(2));
    unknown_branch->statements = { unknown_assignment };
    unknown_branch->else_statements = { loop };
    const auto mode_is_out_of_range = add_statement(
        scope, vhdl::StatementKind::conditional);
    auto out_of_range_branch = std::ranges::find(
        builder.vhdl_hir.mutable_statements(), mode_is_out_of_range,
        &vhdl::Statement::id);
    out_of_range_branch->condition = binary("=", mode_name, one);
    out_of_range_branch->statements = { out_of_range_assignment };
    out_of_range_branch->else_statements = { mode_is_unknown };

    const auto nested_read = expression(vhdl::ExpressionKind::index,
        "index", { values_name,
            expression(vhdl::ExpressionKind::index, "index",
                { values_name, count_name }) });
    const auto unsigned_result = expression(
        vhdl::ExpressionKind::call, "to_unsigned",
        { nested_read, literal(10) });
    const auto cast_result = expression(
        vhdl::ExpressionKind::call, "std_logic_vector", { unsigned_result });
    const auto return_statement = add_statement(
        scope, vhdl::StatementKind::return_statement);
    auto return_record = std::ranges::find(
        builder.vhdl_hir.mutable_statements(), return_statement,
        &vhdl::Statement::id);
    return_record->value = cast_result;

    auto function_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), function,
        &vhdl::Declaration::id);
    function_record->callable.emplace();
    function_record->callable->function = true;
    function_record->callable->pure = true;
    function_record->callable->defined = true;
    function_record->callable->formals = { count, mode };
    function_record->callable->return_type = vector_subtype(9, 0);
    function_record->children = {
        count, mode, array_type_declaration, values
    };
    function_record->statements = {
        mode_is_out_of_range, return_statement
    };

    const auto add_invalid_array_function = [&](const std::string& name,
                                                    const bool two_dimensions,
                                                    const vhdl::ValueDomain
                                                        element_domain) {
        const auto [type_declaration, type] = add_integer_array_type(
            name + "_array_t", two_dimensions, element_domain);
        const auto function_id = add_declaration(scope,
            DeclarationKind::function, vhdl::DeclarationForm::function,
            name);
        const auto values_id = add_declaration(scope,
            DeclarationKind::variable, vhdl::DeclarationForm::variable,
            name + "_values");
        auto values_declaration = std::ranges::find(
            builder.vhdl_hir.mutable_declarations(), values_id,
            &vhdl::Declaration::id);
        values_declaration->declared_type = type;
        values_declaration->initializer
            = add_others_initializer(scope, literal(0));
        const auto invalid_return = add_statement(
            scope, vhdl::StatementKind::return_statement);
        auto invalid_return_record = std::ranges::find(
            builder.vhdl_hir.mutable_statements(), invalid_return,
            &vhdl::Statement::id);
        invalid_return_record->value = literal(0);
        auto invalid_function = std::ranges::find(
            builder.vhdl_hir.mutable_declarations(), function_id,
            &vhdl::Declaration::id);
        invalid_function->callable.emplace();
        invalid_function->callable->function = true;
        invalid_function->callable->pure = true;
        invalid_function->callable->defined = true;
        invalid_function->callable->return_type = integer_subtype();
        invalid_function->children = { type_declaration, values_id };
        invalid_function->statements = { invalid_return };
        builder.vhdl_unit(unit).declarations.push_back(function_id);
        return expression(vhdl::ExpressionKind::call, name, { }, function_id);
    };
    const auto unsupported_shape_call = add_invalid_array_function(
        "unsupported_integer_matrix", true,
        vhdl::ValueDomain::integer);
    const auto unsupported_domain_call = add_invalid_array_function(
        "unsupported_boolean_array", false,
        vhdl::ValueDomain::boolean);
    const auto out_of_range_call = expression(
        vhdl::ExpressionKind::call, "fill_integer_array",
        { literal(512), one }, function);
    const auto unknown_call = expression(
        vhdl::ExpressionKind::call, "fill_integer_array",
        { literal(512), literal(2) }, function);
    const auto positive_call = expression(
        vhdl::ExpressionKind::call, "fill_integer_array",
        { literal(512), literal(0) }, function);
    builder.vhdl_unit(unit).declarations.push_back(function);

    auto design = builder.finish();
    assert(design.valid());
    const std::array<SpecializedHirActualIdentity, 0> actuals { };
    const auto specialized = make_specialized_hir_unit(
        design, unit, actuals);
    assert(specialized);
    const auto positive
        = specialized->evaluate_vhdl_constant_expression(positive_call);
    assert(positive);
    const auto packed = std::get_if<SpecializedHirVhdlPackedValue>(&*positive);
    assert(packed);
    assert(packed->bits == "1000000000");
    assert(packed->left_bound == 9 && packed->right_bound == 0);
    assert(!specialized->evaluate_vhdl_constant_expression(
        out_of_range_call));
    assert(!specialized->evaluate_vhdl_constant_expression(unknown_call));
    assert(!specialized->evaluate_vhdl_constant_expression(
        unsupported_shape_call));
    assert(!specialized->evaluate_vhdl_constant_expression(
        unsupported_domain_call));
}

void test_specialized_hir_vhdl_integer_array_rejects_shadowed_integer()
{
    LinkBundleBuilder builder { "shadowed-integer-array-constant.vhd" };
    const auto unit = builder.add_vhdl_unit(
        UnitKind::vhdl_entity, vhdl::UnitKind::entity,
        "shadowed_integer_array_constant");
    const auto scope = builder.model.units()[unit.value()].scope;
    const auto add_declaration = [&](const DeclarationKind kind,
                                         const vhdl::DeclarationForm form,
                                         const std::string& name) {
        const auto id = builder.model.add_declaration(
            scope, kind, name, builder.source, builder.origin);
        vhdl::Declaration declaration;
        declaration.id = id;
        declaration.scope = scope;
        declaration.form = form;
        declaration.name = name;
        declaration.source = builder.source;
        declaration.origin = builder.origin;
        builder.vhdl_hir.mutable_declarations().push_back(
            std::move(declaration));
        builder.vhdl_unit(unit).declarations.push_back(id);
        return id;
    };
    const auto add_type = [&](const DeclarationId declaration,
                              const std::string& name,
                              const vhdl::TypeForm form) {
        TypeReference base;
        base.source = builder.source;
        const auto type = builder.model.add_type(scope,
            TypeKind::declaration, name, base,
            builder.source, builder.origin);
        const auto declaration_record = std::ranges::find(
            builder.vhdl_hir.mutable_declarations(), declaration,
            &vhdl::Declaration::id);
        declaration_record->declared_type = type;
        vhdl::TypeDefinition definition;
        definition.id = type;
        definition.declaration = declaration;
        definition.form = form;
        definition.name = name;
        definition.source = builder.source;
        definition.origin = builder.origin;
        builder.vhdl_hir.mutable_types().push_back(
            std::move(definition));
        return type;
    };
    const auto shadow_declaration = add_declaration(
        DeclarationKind::type, vhdl::DeclarationForm::type, "integer");
    const auto shadow_type = add_type(
        shadow_declaration, "integer", vhdl::TypeForm::scalar);
    const auto array_declaration = add_declaration(
        DeclarationKind::type, vhdl::DeclarationForm::type,
        "shadow_integer_array_t");
    const auto array_type = add_type(array_declaration,
        "shadow_integer_array_t", vhdl::TypeForm::array);
    auto array_definition = std::ranges::find(
        builder.vhdl_hir.mutable_types(), array_type,
        &vhdl::TypeDefinition::id);
    array_definition->element_subtype.emplace();
    array_definition->element_subtype->type_mark.spelling = "integer";
    array_definition->element_subtype->domain = vhdl::ValueDomain::integer;
    array_definition->element_subtype->executable_width = 32U;
    array_definition->element_subtype->signed_value = true;
    array_definition->element_subtype->integer_storage_width = 32U;
    vhdl::ArrayDimension dimension;
    dimension.index_subtype.spelling = "natural";
    dimension.constraint.emplace();
    dimension.constraint->kind = vhdl::RangeKind::array_index;
    dimension.constraint->left = 0;
    dimension.constraint->right = 1;
    dimension.constraint->source = builder.source;
    dimension.source = builder.source;
    array_definition->array_dimensions.push_back(std::move(dimension));

    const auto values = add_declaration(
        DeclarationKind::variable, vhdl::DeclarationForm::variable,
        "shadowed_values");
    auto values_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), values,
        &vhdl::Declaration::id);
    values_record->declared_type = array_type;
    const auto fill = builder.model.add_expression_identity(
        scope, builder.source, builder.origin);
    vhdl::Expression fill_expression;
    fill_expression.id = fill;
    fill_expression.scope = scope;
    fill_expression.kind = vhdl::ExpressionKind::integer_literal;
    fill_expression.text = "0";
    fill_expression.source = builder.source;
    fill_expression.origin = builder.origin;
    builder.vhdl_hir.mutable_expressions().push_back(
        std::move(fill_expression));
    const auto aggregate = builder.model.add_expression_identity(
        scope, builder.source, builder.origin);
    vhdl::Expression aggregate_expression;
    aggregate_expression.id = aggregate;
    aggregate_expression.scope = scope;
    aggregate_expression.kind = vhdl::ExpressionKind::aggregate;
    aggregate_expression.text = "(...)";
    aggregate_expression.source = builder.source;
    aggregate_expression.origin = builder.origin;
    vhdl::AggregateAssociation others;
    others.choice_spelling = "others";
    others.value = fill;
    others.source = builder.source;
    aggregate_expression.associations.push_back(std::move(others));
    builder.vhdl_hir.mutable_expressions().push_back(
        std::move(aggregate_expression));
    values_record->initializer = aggregate;

    const auto function = add_declaration(
        DeclarationKind::function, vhdl::DeclarationForm::function,
        "read_shadowed_integer_array");
    const auto result_name = builder.model.add_expression_identity(
        scope, builder.source, builder.origin);
    vhdl::Expression name_expression;
    name_expression.id = result_name;
    name_expression.scope = scope;
    name_expression.kind = vhdl::ExpressionKind::name;
    name_expression.text = "shadowed_values";
    name_expression.source = builder.source;
    name_expression.origin = builder.origin;
    name_expression.referenced_name.emplace();
    name_expression.referenced_name->spelling = "shadowed_values";
    name_expression.referenced_name->canonical = "shadowed_values";
    name_expression.referenced_name->selected = values;
    builder.vhdl_hir.mutable_expressions().push_back(
        std::move(name_expression));
    const auto zero = builder.model.add_expression_identity(
        scope, builder.source, builder.origin);
    vhdl::Expression zero_expression;
    zero_expression.id = zero;
    zero_expression.scope = scope;
    zero_expression.kind = vhdl::ExpressionKind::integer_literal;
    zero_expression.text = "0";
    zero_expression.source = builder.source;
    zero_expression.origin = builder.origin;
    builder.vhdl_hir.mutable_expressions().push_back(
        std::move(zero_expression));
    const auto index = builder.model.add_expression_identity(
        scope, builder.source, builder.origin);
    vhdl::Expression index_expression;
    index_expression.id = index;
    index_expression.scope = scope;
    index_expression.kind = vhdl::ExpressionKind::index;
    index_expression.text = "index";
    index_expression.operands = { result_name, zero };
    index_expression.source = builder.source;
    index_expression.origin = builder.origin;
    builder.vhdl_hir.mutable_expressions().push_back(
        std::move(index_expression));
    const auto return_statement = builder.model.add_statement_identity(
        scope, builder.source, builder.origin);
    vhdl::Statement statement;
    statement.id = return_statement;
    statement.scope = scope;
    statement.kind = vhdl::StatementKind::return_statement;
    statement.value = index;
    statement.source = builder.source;
    statement.origin = builder.origin;
    builder.vhdl_hir.mutable_statements().push_back(
        std::move(statement));
    auto function_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), function,
        &vhdl::Declaration::id);
    function_record->callable.emplace();
    function_record->callable->function = true;
    function_record->callable->pure = true;
    function_record->callable->defined = true;
    function_record->callable->return_type.emplace();
    function_record->callable->return_type->type_mark.spelling = "integer";
    function_record->callable->return_type->domain
        = vhdl::ValueDomain::integer;
    function_record->children = { values };
    function_record->statements = { return_statement };

    const auto call = builder.model.add_expression_identity(
        scope, builder.source, builder.origin);
    vhdl::Expression call_expression;
    call_expression.id = call;
    call_expression.scope = scope;
    call_expression.kind = vhdl::ExpressionKind::call;
    call_expression.text = "read_shadowed_integer_array";
    call_expression.source = builder.source;
    call_expression.origin = builder.origin;
    call_expression.referenced_name.emplace();
    call_expression.referenced_name->spelling
        = "read_shadowed_integer_array";
    call_expression.referenced_name->canonical
        = "read_shadowed_integer_array";
    call_expression.referenced_name->selected = function;
    builder.vhdl_hir.mutable_expressions().push_back(
        std::move(call_expression));

    auto design = builder.finish();
    assert(design.valid());
    const auto shadow_types = design.vhdl_type_declarations_named("integer");
    assert(shadow_types && shadow_types->size() == 1U);
    const auto resolved_shadow_declaration = design.find_declaration(
        *shadow_types->begin());
    assert(resolved_shadow_declaration
        && resolved_shadow_declaration->vhdl != nullptr
        && resolved_shadow_declaration->vhdl->declared_type == shadow_type);

    const std::array<SpecializedHirActualIdentity, 0> actuals { };
    const auto specialized = make_specialized_hir_unit(
        design, unit, actuals);
    assert(specialized);
    assert(!specialized->evaluate_vhdl_constant_expression(call));
}

void test_specialized_hir_vhdl_packed_array_projection()
{
    LinkBundleBuilder builder { "packed-array-constant-projection.vhd" };
    const auto unit = builder.add_vhdl_unit(
        UnitKind::vhdl_entity, vhdl::UnitKind::entity,
        "packed_array_constant_projection");
    const auto scope = builder.model.units()[unit.value()].scope;
    const auto add_expression
        = [&](const vhdl::ExpressionKind kind, std::string text,
              std::vector<ExpressionId> operands = { },
              const std::optional<DeclarationId> selected = std::nullopt) {
              const auto id = builder.model.add_expression_identity(
                  scope, builder.source, builder.origin);
              vhdl::Expression expression;
              expression.id = id;
              expression.scope = scope;
              expression.kind = kind;
              expression.text = std::move(text);
              expression.operands = std::move(operands);
              expression.source = builder.source;
              expression.origin = builder.origin;
              if (selected) {
                  expression.referenced_name.emplace();
                  expression.referenced_name->spelling
                      = expression.text;
                  expression.referenced_name->canonical
                      = expression.text;
                  expression.referenced_name->selected = selected;
              }
              builder.vhdl_hir.mutable_expressions().push_back(
                  std::move(expression));
              return id;
          };
    const auto literal = [&](const std::int64_t value) {
        return add_expression(vhdl::ExpressionKind::integer_literal,
            std::to_string(value));
    };
    const auto add_declaration
        = [&](const DeclarationKind kind,
              const vhdl::DeclarationForm form, std::string name,
              const std::optional<ExpressionId> initializer = std::nullopt) {
              const auto id = builder.model.add_declaration(
                  scope, kind, name, builder.source, builder.origin);
              vhdl::Declaration declaration;
              declaration.id = id;
              declaration.scope = scope;
              declaration.form = form;
              declaration.name = std::move(name);
              declaration.initializer = initializer;
              declaration.source = builder.source;
              declaration.origin = builder.origin;
              builder.vhdl_hir.mutable_declarations().push_back(
                  std::move(declaration));
              return id;
          };
    const auto array_type = [&](const std::string& name,
                                const std::int64_t outer_left,
                                const std::int64_t outer_right,
                                const std::int64_t element_left) {
        const auto declaration = add_declaration(
            DeclarationKind::type, vhdl::DeclarationForm::type, name);
        TypeReference base;
        base.source = builder.source;
        base.spelling = name;
        const auto type = builder.model.add_type(scope,
            TypeKind::declaration, name, base,
            builder.source, builder.origin);
        auto declaration_record = std::ranges::find(
            builder.vhdl_hir.mutable_declarations(), declaration,
            &vhdl::Declaration::id);
        declaration_record->declared_type = type;

        vhdl::TypeDefinition definition;
        definition.id = type;
        definition.declaration = declaration;
        definition.form = vhdl::TypeForm::array;
        definition.name = name;
        definition.source = builder.source;
        definition.origin = builder.origin;
        definition.element_subtype.emplace();
        definition.element_subtype->type_mark.spelling
            = "std_logic_vector";
        definition.element_subtype->domain = vhdl::ValueDomain::logic9;
        vhdl::RangeConstraint element_range;
        element_range.kind = vhdl::RangeKind::array_index;
        element_range.left = element_left;
        element_range.right = 0;
        element_range.descending = true;
        element_range.source = builder.source;
        definition.element_subtype->constraints.push_back(
            std::move(element_range));
        vhdl::ArrayDimension dimension;
        dimension.index_subtype.spelling = "integer";
        dimension.constraint.emplace();
        dimension.constraint->kind = vhdl::RangeKind::array_index;
        dimension.constraint->left = outer_left;
        dimension.constraint->right = outer_right;
        dimension.constraint->descending = outer_left >= outer_right;
        dimension.constraint->source = builder.source;
        dimension.source = builder.source;
        definition.array_dimensions.push_back(std::move(dimension));
        builder.vhdl_hir.mutable_types().push_back(std::move(definition));
        builder.vhdl_unit(unit).declarations.push_back(declaration);
        return type;
    };
    const auto packed_array_type = array_type("rom_t", 1, 0, 3);
    const auto wrong_outer_type = array_type(
        "wrong_outer_t", 2, 0, 3);
    const auto wrong_element_type = array_type(
        "wrong_element_t", 1, 0, 2);
    const auto subtype = [&](const TypeId type, const std::string& name) {
        vhdl::SubtypeIndication result;
        result.type_mark.target = type;
        result.type_mark.spelling = name;
        return result;
    };
    const auto add_array_function
        = [&](const std::string& function_name,
              const TypeId return_type,
              const std::string& return_type_name,
              const TypeId local_type,
              const std::int64_t local_element_width,
              const bool initialize, const bool replace_second_element) {
              const auto function = add_declaration(
                  DeclarationKind::function,
                  vhdl::DeclarationForm::function, function_name);
              const auto local = add_declaration(
                  DeclarationKind::variable,
                  vhdl::DeclarationForm::variable,
                  function_name + "_value");
              auto local_record = std::ranges::find(
                  builder.vhdl_hir.mutable_declarations(), local,
                  &vhdl::Declaration::id);
              local_record->declared_type = local_type;
              if (initialize) {
                  const auto aggregate = add_expression(
                      vhdl::ExpressionKind::aggregate, "(...)");
                  const auto others = add_expression(
                      vhdl::ExpressionKind::default_choice, "others");
                  const auto fill = add_expression(
                      vhdl::ExpressionKind::call, "to_unsigned",
                      { literal(5), literal(local_element_width) });
                  vhdl::AggregateAssociation association;
                  association.choices.push_back(others);
                  association.value = fill;
                  association.source = builder.source;
                  auto aggregate_record = std::ranges::find(
                      builder.vhdl_hir.mutable_expressions(), aggregate,
                      &vhdl::Expression::id);
                  aggregate_record->associations.push_back(
                      std::move(association));
                  local_record->initializer = aggregate;
              }

              std::vector<StatementId> statements;
              if (replace_second_element) {
                  const auto target = add_expression(
                      vhdl::ExpressionKind::index, "index",
                      { add_expression(vhdl::ExpressionKind::name,
                            function_name + "_value", { }, local),
                          literal(0) });
                  const auto replacement = add_expression(
                      vhdl::ExpressionKind::call, "to_unsigned",
                      { literal(10), literal(4) });
                  const auto assignment = builder.model
                      .add_statement_identity(
                          scope, builder.source, builder.origin);
                  vhdl::Statement assignment_record;
                  assignment_record.id = assignment;
                  assignment_record.scope = scope;
                  assignment_record.kind
                      = vhdl::StatementKind::variable_assignment;
                  assignment_record.target = target;
                  assignment_record.value = replacement;
                  assignment_record.source = builder.source;
                  assignment_record.origin = builder.origin;
                  builder.vhdl_hir.mutable_statements().push_back(
                      std::move(assignment_record));
                  statements.push_back(assignment);
              }
              const auto returned = add_expression(
                  vhdl::ExpressionKind::name,
                  function_name + "_value", { }, local);
              const auto return_statement = builder.model
                  .add_statement_identity(
                      scope, builder.source, builder.origin);
              vhdl::Statement return_record;
              return_record.id = return_statement;
              return_record.scope = scope;
              return_record.kind
                  = vhdl::StatementKind::return_statement;
              return_record.value = returned;
              return_record.source = builder.source;
              return_record.origin = builder.origin;
              builder.vhdl_hir.mutable_statements().push_back(
                  std::move(return_record));
              statements.push_back(return_statement);

              auto function_record = std::ranges::find(
                  builder.vhdl_hir.mutable_declarations(), function,
                  &vhdl::Declaration::id);
              function_record->callable.emplace();
              function_record->callable->function = true;
              function_record->callable->pure = true;
              function_record->callable->defined = true;
              function_record->callable->return_type
                  = subtype(return_type, return_type_name);
              function_record->children = { local };
              function_record->statements = std::move(statements);
              builder.vhdl_unit(unit).declarations.push_back(function);
              return add_expression(vhdl::ExpressionKind::call,
                  function_name, { }, function);
          };

    const auto packed_array_call = add_array_function(
        "make_rom", packed_array_type, "rom_t", packed_array_type,
        4, true, true);
    const auto wrong_outer_call = add_array_function(
        "make_wrong_outer", packed_array_type, "rom_t",
        wrong_outer_type, 4, true, false);
    const auto wrong_element_call = add_array_function(
        "make_wrong_element", packed_array_type, "rom_t",
        wrong_element_type, 3, true, false);
    const auto incomplete_call = add_array_function(
        "make_incomplete", packed_array_type, "rom_t",
        packed_array_type, 4, false, false);

    const auto rom = add_declaration(DeclarationKind::constant,
        vhdl::DeclarationForm::constant, "ROM", packed_array_call);
    auto rom_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), rom,
        &vhdl::Declaration::id);
    rom_record->declared_type = packed_array_type;
    builder.vhdl_unit(unit).declarations.push_back(rom);
    const auto wrong_rom = add_declaration(DeclarationKind::constant,
        vhdl::DeclarationForm::constant, "WRONG_ROM", wrong_outer_call);
    auto wrong_rom_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), wrong_rom,
        &vhdl::Declaration::id);
    wrong_rom_record->declared_type = packed_array_type;
    builder.vhdl_unit(unit).declarations.push_back(wrong_rom);
    const auto incomplete_rom = add_declaration(
        DeclarationKind::constant, vhdl::DeclarationForm::constant,
        "INCOMPLETE_ROM", incomplete_call);
    auto incomplete_rom_record = std::ranges::find(
        builder.vhdl_hir.mutable_declarations(), incomplete_rom,
        &vhdl::Declaration::id);
    incomplete_rom_record->declared_type = packed_array_type;
    builder.vhdl_unit(unit).declarations.push_back(incomplete_rom);

    auto design = builder.finish();
    assert(design.valid());
    const std::array<SpecializedHirActualIdentity, 0> actuals { };
    const auto specialized = make_specialized_hir_unit(
        design, unit, actuals);
    assert(specialized);

    const auto expression_value
        = specialized->evaluate_vhdl_packed_array_expression(
            packed_array_call);
    assert(expression_value);
    assert(expression_value->left_bound == 1);
    assert(expression_value->right_bound == 0);
    assert(expression_value->element_domain == vhdl::ValueDomain::logic9);
    assert(expression_value->elements.size() == 2U);
    assert(expression_value->elements[0].bits == "0101");
    assert(expression_value->elements[0].left_bound == 3);
    assert(expression_value->elements[0].right_bound == 0);
    assert(expression_value->elements[1].bits == "1010");
    assert(expression_value->elements[1].left_bound == 3);
    assert(expression_value->elements[1].right_bound == 0);

    const auto declaration_value
        = specialized->evaluate_vhdl_packed_array_declaration(rom);
    assert(declaration_value);
    assert(*declaration_value == *expression_value);
    assert(!specialized->evaluate_vhdl_packed_array_declaration(wrong_rom));
    assert(!specialized->evaluate_vhdl_packed_array_declaration(
        incomplete_rom));
    assert(!specialized->evaluate_vhdl_packed_array_expression(
        wrong_outer_call));
    assert(!specialized->evaluate_vhdl_packed_array_expression(
        wrong_element_call));
    assert(!specialized->evaluate_vhdl_packed_array_expression(
        incomplete_call));
    assert(!specialized->evaluate_vhdl_constant_expression(
        packed_array_call));
}

void test_compiled_design_normalization()
{
    auto design = make_compiled_design();
    auto& systemverilog = design.mutable_systemverilog().mutable_expressions();
    const auto sv_add_source = design.systemverilog_units().front().source;
    const auto sv_add_origin = design.systemverilog_units().front().origin;
    systemverilog.push_back(systemverilog_expression(
        design, 0, sv::ExpressionKind::integer_literal, "7"));
    systemverilog.push_back(systemverilog_expression(
        design, 1, sv::ExpressionKind::integer_literal, "5"));
    systemverilog.push_back(systemverilog_expression(
        design, 2, sv::ExpressionKind::binary, "+",
        { ExpressionId::from_index(0), ExpressionId::from_index(1) }));
    systemverilog.push_back(systemverilog_expression(
        design, 3, sv::ExpressionKind::unary, "-",
        { ExpressionId::from_index(2) }));
    systemverilog.push_back(systemverilog_expression(
        design, 4, sv::ExpressionKind::boolean_literal, "true"));
    systemverilog.push_back(systemverilog_expression(
        design, 5, sv::ExpressionKind::boolean_literal, "false"));
    systemverilog.push_back(systemverilog_expression(
        design, 6, sv::ExpressionKind::binary, "&&",
        { ExpressionId::from_index(4), ExpressionId::from_index(5) }));
    systemverilog.push_back(systemverilog_expression(
        design, 7, sv::ExpressionKind::unary, "!",
        { ExpressionId::from_index(6) }));
    systemverilog.push_back(systemverilog_expression(
        design, 8, sv::ExpressionKind::binary, "<",
        { ExpressionId::from_index(1), ExpressionId::from_index(0) }));
    systemverilog.push_back(systemverilog_expression(
        design, 9, sv::ExpressionKind::integer_literal, "0"));
    systemverilog.push_back(systemverilog_expression(
        design, 10, sv::ExpressionKind::binary, "/",
        { ExpressionId::from_index(2), ExpressionId::from_index(9) }));
    systemverilog.push_back(systemverilog_expression(
        design, 11, sv::ExpressionKind::integer_literal,
        std::to_string(std::numeric_limits<std::int64_t>::max())));
    systemverilog.push_back(systemverilog_expression(
        design, 12, sv::ExpressionKind::integer_literal, "1"));
    systemverilog.push_back(systemverilog_expression(
        design, 13, sv::ExpressionKind::binary, "+",
        { ExpressionId::from_index(11), ExpressionId::from_index(12) }));
    systemverilog.push_back(systemverilog_expression(
        design, 14, sv::ExpressionKind::binary, "<<",
        { ExpressionId::from_index(0), ExpressionId::from_index(1) }));
    auto dependent = systemverilog_expression(
        design, 15, sv::ExpressionKind::binary, "+",
        { ExpressionId::from_index(0), ExpressionId::from_index(1) });
    sv::Name unresolved;
    unresolved.spelling = "PARAMETER";
    unresolved.source = dependent.source;
    dependent.referenced_name = std::move(unresolved);
    systemverilog.push_back(std::move(dependent));
    systemverilog.push_back(systemverilog_expression(
        design, 16, sv::ExpressionKind::binary, "*",
        { ExpressionId::from_index(0), ExpressionId::from_index(1) }));
    systemverilog.push_back(systemverilog_expression(
        design, 17, sv::ExpressionKind::binary, "-",
        { ExpressionId::from_index(0), ExpressionId::from_index(1) }));
    systemverilog.push_back(systemverilog_expression(
        design, 18, sv::ExpressionKind::binary, "%",
        { ExpressionId::from_index(0), ExpressionId::from_index(1) }));
    systemverilog.push_back(systemverilog_expression(
        design, 19, sv::ExpressionKind::binary, ">=",
        { ExpressionId::from_index(0), ExpressionId::from_index(1) }));

    const auto sv_scope = design.systemverilog_units().front().scope;
    const auto sv_local_parameter = design.mutable_semantics().add_declaration(
        sv_scope, DeclarationKind::constant, "LOCAL_CONSTANT",
        sv_add_source, sv_add_origin);
    sv::Declaration sv_local_parameter_record;
    sv_local_parameter_record.id = sv_local_parameter;
    sv_local_parameter_record.scope = sv_scope;
    sv_local_parameter_record.form = sv::DeclarationForm::local_parameter;
    sv_local_parameter_record.name = "LOCAL_CONSTANT";
    sv_local_parameter_record.source = sv_add_source;
    sv_local_parameter_record.origin = sv_add_origin;
    sv_local_parameter_record.initializer = ExpressionId::from_index(2);
    design.mutable_systemverilog().mutable_declarations().push_back(
        sv_local_parameter_record);
    design.mutable_systemverilog().mutable_units().front()
        .declarations.push_back(sv_local_parameter);
    auto sv_constant_name = systemverilog_expression(
        design, 20, sv::ExpressionKind::name, "LOCAL_CONSTANT");
    sv::Name sv_constant_reference;
    sv_constant_reference.spelling = "LOCAL_CONSTANT";
    sv_constant_reference.source = sv_constant_name.source;
    sv_constant_reference.selected = sv_local_parameter;
    sv_constant_name.referenced_name = sv_constant_reference;
    systemverilog.push_back(std::move(sv_constant_name));

    const auto sv_parameter = design.mutable_semantics().add_declaration(
        sv_scope, DeclarationKind::generic, "OVERRIDABLE",
        sv_add_source, sv_add_origin);
    sv::Declaration sv_parameter_record;
    sv_parameter_record.id = sv_parameter;
    sv_parameter_record.scope = sv_scope;
    sv_parameter_record.form = sv::DeclarationForm::parameter;
    sv_parameter_record.name = "OVERRIDABLE";
    sv_parameter_record.source = sv_add_source;
    sv_parameter_record.origin = sv_add_origin;
    sv_parameter_record.initializer = ExpressionId::from_index(2);
    design.mutable_systemverilog().mutable_declarations().push_back(
        sv_parameter_record);
    design.mutable_systemverilog().mutable_units().front()
        .declarations.push_back(sv_parameter);
    auto sv_parameter_name = systemverilog_expression(
        design, 21, sv::ExpressionKind::name, "OVERRIDABLE");
    sv::Name sv_parameter_reference;
    sv_parameter_reference.spelling = "OVERRIDABLE";
    sv_parameter_reference.source = sv_parameter_name.source;
    sv_parameter_reference.selected = sv_parameter;
    sv_parameter_name.referenced_name = sv_parameter_reference;
    systemverilog.push_back(std::move(sv_parameter_name));

    systemverilog.push_back(systemverilog_expression(
        design, 22, sv::ExpressionKind::default_choice, "default"));
    auto sv_rom = systemverilog_expression(
        design, 23, sv::ExpressionKind::assignment_pattern, "'{...}");
    sv::AssignmentPatternAssociation sv_rom_association;
    sv_rom_association.choices.push_back(ExpressionId::from_index(22));
    sv_rom_association.value = ExpressionId::from_index(2);
    sv_rom_association.source = sv_rom.source;
    sv_rom.associations.push_back(std::move(sv_rom_association));
    systemverilog.push_back(std::move(sv_rom));

    auto sv_hierarchy_name = systemverilog_expression(
        design, 24, sv::ExpressionKind::name, "top.runtime_value");
    sv::Name sv_hierarchy_reference;
    sv_hierarchy_reference.spelling = "top.runtime_value";
    sv_hierarchy_reference.source = sv_hierarchy_name.source;
    sv_hierarchy_name.referenced_name = sv_hierarchy_reference;
    systemverilog.push_back(std::move(sv_hierarchy_name));
    auto sv_residual_aggregate = systemverilog_expression(
        design, 25, sv::ExpressionKind::assignment_pattern, "'{...}");
    sv::AssignmentPatternAssociation sv_residual_association;
    sv_residual_association.value = ExpressionId::from_index(24);
    sv_residual_association.source = sv_residual_aggregate.source;
    sv_residual_aggregate.associations.push_back(
        std::move(sv_residual_association));
    systemverilog.push_back(std::move(sv_residual_aggregate));
    auto sv_associated_call = systemverilog_expression(
        design, 26, sv::ExpressionKind::call, "identity");
    sv::CallAssociation sv_call_argument;
    sv_call_argument.actual = ExpressionId::from_index(24);
    sv_call_argument.source = sv_associated_call.source;
    sv_associated_call.call_arguments.push_back(
        std::move(sv_call_argument));
    systemverilog.push_back(std::move(sv_associated_call));

    const auto sv_type_declaration
        = design.mutable_semantics().add_declaration(
            sv_scope, DeclarationKind::type, "folded_vector_t",
            sv_add_source, sv_add_origin);
    TypeReference sv_semantic_type;
    sv_semantic_type.source = sv_add_source;
    sv_semantic_type.spelling = "logic";
    const auto sv_type = design.mutable_semantics().add_type(
        sv_scope, TypeKind::declaration, "folded_vector_t",
        sv_semantic_type, sv_add_source, sv_add_origin);
    sv::TypeDefinition sv_type_record;
    sv_type_record.id = sv_type;
    sv_type_record.declaration = sv_type_declaration;
    sv_type_record.name = "folded_vector_t";
    sv_type_record.base.target.source = sv_add_source;
    sv_type_record.base.target.spelling = "logic";
    sv_type_record.source = sv_add_source;
    sv_type_record.origin = sv_add_origin;
    sv::PackedRange sv_range;
    sv_range.left_expression = ExpressionId::from_index(2);
    sv_range.right_expression = ExpressionId::from_index(1);
    sv_range.descending = true;
    sv_range.source = sv_add_source;
    sv_type_record.base.packed_range = sv_range;
    design.mutable_systemverilog().mutable_types().push_back(
        std::move(sv_type_record));
    sv::Declaration sv_type_declaration_record;
    sv_type_declaration_record.id = sv_type_declaration;
    sv_type_declaration_record.scope = sv_scope;
    sv_type_declaration_record.form
        = sv::DeclarationForm::typedef_declaration;
    sv_type_declaration_record.name = "folded_vector_t";
    sv_type_declaration_record.source = sv_add_source;
    sv_type_declaration_record.origin = sv_add_origin;
    sv_type_declaration_record.declared_type = sv_type;
    design.mutable_systemverilog().mutable_declarations().push_back(
        std::move(sv_type_declaration_record));
    design.mutable_systemverilog().mutable_units().front()
        .declarations.push_back(sv_type_declaration);

    auto& vhdl = design.mutable_vhdl().mutable_expressions();
    vhdl.push_back(vhdl_expression(
        design, 0, vhdl::ExpressionKind::integer_literal, "-5"));
    vhdl.push_back(vhdl_expression(
        design, 1, vhdl::ExpressionKind::integer_literal, "3"));
    vhdl.push_back(vhdl_expression(
        design, 2, vhdl::ExpressionKind::binary, "MOD",
        { ExpressionId::from_index(0), ExpressionId::from_index(1) }));
    vhdl.push_back(vhdl_expression(
        design, 3, vhdl::ExpressionKind::binary, "rem",
        { ExpressionId::from_index(0), ExpressionId::from_index(1) }));
    vhdl.push_back(vhdl_expression(
        design, 4, vhdl::ExpressionKind::binary, "*",
        { ExpressionId::from_index(0), ExpressionId::from_index(1) }));
    vhdl.push_back(vhdl_expression(
        design, 5, vhdl::ExpressionKind::binary, "/",
        { ExpressionId::from_index(0), ExpressionId::from_index(1) }));
    vhdl.push_back(vhdl_expression(
        design, 6, vhdl::ExpressionKind::binary, "/=",
        { ExpressionId::from_index(0), ExpressionId::from_index(1) }));
    vhdl.push_back(vhdl_expression(
        design, 7, vhdl::ExpressionKind::boolean_literal, "TRUE"));
    vhdl.push_back(vhdl_expression(
        design, 8, vhdl::ExpressionKind::boolean_literal, "false"));
    vhdl.push_back(vhdl_expression(
        design, 9, vhdl::ExpressionKind::binary, "OR",
        { ExpressionId::from_index(7), ExpressionId::from_index(8) }));
    vhdl.push_back(vhdl_expression(
        design, 10, vhdl::ExpressionKind::binary, "xor",
        { ExpressionId::from_index(7), ExpressionId::from_index(8) }));
    vhdl.push_back(vhdl_expression(
        design, 11, vhdl::ExpressionKind::unary, "not",
        { ExpressionId::from_index(7) }));
    vhdl.push_back(vhdl_expression(
        design, 12, vhdl::ExpressionKind::unary, "abs",
        { ExpressionId::from_index(0) }));
    auto width_dependent = vhdl_expression(
        design, 13, vhdl::ExpressionKind::binary, "+",
        { ExpressionId::from_index(0), ExpressionId::from_index(1) });
    width_dependent.nominal_type = "signed(7 downto 0)";
    vhdl.push_back(std::move(width_dependent));

    const auto vhdl_scope = design.vhdl_units().front().scope;
    const auto vhdl_constant = design.mutable_semantics().add_declaration(
        vhdl_scope, DeclarationKind::constant, "local_constant",
        sv_add_source, sv_add_origin);
    vhdl::Declaration vhdl_constant_record;
    vhdl_constant_record.id = vhdl_constant;
    vhdl_constant_record.scope = vhdl_scope;
    vhdl_constant_record.form = vhdl::DeclarationForm::constant;
    vhdl_constant_record.name = "local_constant";
    vhdl_constant_record.source = sv_add_source;
    vhdl_constant_record.origin = sv_add_origin;
    vhdl_constant_record.initializer = ExpressionId::from_index(2);
    design.mutable_vhdl().mutable_declarations().push_back(
        vhdl_constant_record);
    design.mutable_vhdl().mutable_units().front()
        .declarations.push_back(vhdl_constant);
    auto vhdl_constant_name = vhdl_expression(
        design, 14, vhdl::ExpressionKind::name, "local_constant");
    vhdl::Name vhdl_constant_reference;
    vhdl_constant_reference.spelling = "local_constant";
    vhdl_constant_reference.canonical = "local_constant";
    vhdl_constant_reference.source = vhdl_constant_name.source;
    vhdl_constant_reference.selected = vhdl_constant;
    vhdl_constant_name.referenced_name = vhdl_constant_reference;
    vhdl.push_back(std::move(vhdl_constant_name));

    const auto vhdl_formal = design.mutable_semantics().add_declaration(
        vhdl_scope, DeclarationKind::constant, "value",
        sv_add_source, sv_add_origin);
    vhdl::Declaration vhdl_formal_record;
    vhdl_formal_record.id = vhdl_formal;
    vhdl_formal_record.scope = vhdl_scope;
    vhdl_formal_record.form = vhdl::DeclarationForm::constant;
    vhdl_formal_record.name = "value";
    vhdl_formal_record.source = sv_add_source;
    vhdl_formal_record.origin = sv_add_origin;
    vhdl_formal_record.initializer = ExpressionId::from_index(1);
    design.mutable_vhdl().mutable_declarations().push_back(
        vhdl_formal_record);
    auto vhdl_formal_name = vhdl_expression(
        design, 15, vhdl::ExpressionKind::name, "value");
    vhdl::Name vhdl_formal_reference;
    vhdl_formal_reference.spelling = "value";
    vhdl_formal_reference.canonical = "value";
    vhdl_formal_reference.source = vhdl_formal_name.source;
    vhdl_formal_reference.selected = vhdl_formal;
    vhdl_formal_name.referenced_name = vhdl_formal_reference;
    vhdl.push_back(std::move(vhdl_formal_name));
    vhdl.push_back(vhdl_expression(
        design, 16, vhdl::ExpressionKind::binary, "+",
        { ExpressionId::from_index(15), ExpressionId::from_index(1) }));

    const auto vhdl_function = design.mutable_semantics().add_declaration(
        vhdl_scope, DeclarationKind::function, "add_three",
        sv_add_source, sv_add_origin);
    const auto vhdl_return = design.mutable_semantics()
                                 .add_statement_identity(
                                     vhdl_scope, sv_add_source, sv_add_origin);
    vhdl::Statement vhdl_return_record;
    vhdl_return_record.id = vhdl_return;
    vhdl_return_record.scope = vhdl_scope;
    vhdl_return_record.kind = vhdl::StatementKind::return_statement;
    vhdl_return_record.value = ExpressionId::from_index(16);
    vhdl_return_record.source = sv_add_source;
    vhdl_return_record.origin = sv_add_origin;
    design.mutable_vhdl().mutable_statements().push_back(
        vhdl_return_record);
    vhdl::Declaration vhdl_function_record;
    vhdl_function_record.id = vhdl_function;
    vhdl_function_record.scope = vhdl_scope;
    vhdl_function_record.form = vhdl::DeclarationForm::function;
    vhdl_function_record.name = "add_three";
    vhdl_function_record.source = sv_add_source;
    vhdl_function_record.origin = sv_add_origin;
    vhdl_function_record.callable.emplace();
    vhdl_function_record.callable->function = true;
    vhdl_function_record.callable->pure = true;
    vhdl_function_record.callable->defined = true;
    vhdl_function_record.callable->formals.push_back(vhdl_formal);
    vhdl_function_record.statements.push_back(vhdl_return);
    design.mutable_vhdl().mutable_declarations().push_back(
        vhdl_function_record);
    design.mutable_vhdl().mutable_units().front()
        .declarations.push_back(vhdl_function);
    auto vhdl_call = vhdl_expression(
        design, 17, vhdl::ExpressionKind::call, "add_three");
    vhdl::Name vhdl_function_reference;
    vhdl_function_reference.spelling = "add_three";
    vhdl_function_reference.canonical = "add_three";
    vhdl_function_reference.source = vhdl_call.source;
    vhdl_function_reference.selected = vhdl_function;
    vhdl_call.referenced_name = vhdl_function_reference;
    vhdl.push_back(std::move(vhdl_call));

    const auto vhdl_impure_function
        = design.mutable_semantics().add_declaration(
            vhdl_scope, DeclarationKind::function, "impure_add_three",
            sv_add_source, sv_add_origin);
    auto vhdl_impure_record = vhdl_function_record;
    vhdl_impure_record.id = vhdl_impure_function;
    vhdl_impure_record.name = "impure_add_three";
    vhdl_impure_record.callable->pure = false;
    design.mutable_vhdl().mutable_declarations().push_back(
        vhdl_impure_record);
    design.mutable_vhdl().mutable_units().front()
        .declarations.push_back(vhdl_impure_function);
    auto vhdl_impure_call = vhdl_expression(
        design, 18, vhdl::ExpressionKind::call, "impure_add_three",
        { ExpressionId::from_index(1) });
    auto vhdl_impure_reference = vhdl_function_reference;
    vhdl_impure_reference.spelling = "impure_add_three";
    vhdl_impure_reference.canonical = "impure_add_three";
    vhdl_impure_reference.selected = vhdl_impure_function;
    vhdl_impure_call.referenced_name = vhdl_impure_reference;
    vhdl.push_back(std::move(vhdl_impure_call));

    vhdl.push_back(vhdl_expression(
        design, 19, vhdl::ExpressionKind::default_choice, "others"));
    auto vhdl_rom = vhdl_expression(
        design, 20, vhdl::ExpressionKind::aggregate, "(...)");
    vhdl::AggregateAssociation vhdl_rom_association;
    vhdl_rom_association.choices.push_back(ExpressionId::from_index(19));
    vhdl_rom_association.value = ExpressionId::from_index(2);
    vhdl_rom_association.source = vhdl_rom.source;
    vhdl_rom.associations.push_back(std::move(vhdl_rom_association));
    vhdl.push_back(std::move(vhdl_rom));
    const auto vhdl_rom_declaration
        = design.mutable_semantics().add_declaration(
            vhdl_scope, DeclarationKind::constant, "rom",
            sv_add_source, sv_add_origin);
    auto vhdl_rom_record = vhdl_constant_record;
    vhdl_rom_record.id = vhdl_rom_declaration;
    vhdl_rom_record.name = "rom";
    vhdl_rom_record.initializer = ExpressionId::from_index(20);
    design.mutable_vhdl().mutable_declarations().push_back(
        vhdl_rom_record);
    design.mutable_vhdl().mutable_units().front()
        .declarations.push_back(vhdl_rom_declaration);

    auto vhdl_hierarchy_name = vhdl_expression(
        design, 21, vhdl::ExpressionKind::name, "top.runtime_value");
    vhdl::Name vhdl_hierarchy_reference;
    vhdl_hierarchy_reference.spelling = "top.runtime_value";
    vhdl_hierarchy_reference.canonical = "top.runtime_value";
    vhdl_hierarchy_reference.source = vhdl_hierarchy_name.source;
    vhdl_hierarchy_name.referenced_name = vhdl_hierarchy_reference;
    vhdl.push_back(std::move(vhdl_hierarchy_name));
    auto vhdl_residual_aggregate = vhdl_expression(
        design, 22, vhdl::ExpressionKind::aggregate, "(...)");
    vhdl::AggregateAssociation vhdl_residual_association;
    vhdl_residual_association.value = ExpressionId::from_index(21);
    vhdl_residual_association.source = vhdl_residual_aggregate.source;
    vhdl_residual_aggregate.associations.push_back(
        std::move(vhdl_residual_association));
    vhdl.push_back(std::move(vhdl_residual_aggregate));

    const auto vhdl_recursive_function
        = design.mutable_semantics().add_declaration(
            vhdl_scope, DeclarationKind::function, "recursive_constant",
            sv_add_source, sv_add_origin);
    auto vhdl_recursive_call = vhdl_expression(
        design, 23, vhdl::ExpressionKind::call, "recursive_constant");
    auto vhdl_recursive_reference = vhdl_function_reference;
    vhdl_recursive_reference.spelling = "recursive_constant";
    vhdl_recursive_reference.canonical = "recursive_constant";
    vhdl_recursive_reference.selected = vhdl_recursive_function;
    vhdl_recursive_call.referenced_name = vhdl_recursive_reference;
    vhdl.push_back(std::move(vhdl_recursive_call));
    const auto vhdl_recursive_return
        = design.mutable_semantics().add_statement_identity(
            vhdl_scope, sv_add_source, sv_add_origin);
    auto vhdl_recursive_return_record = vhdl_return_record;
    vhdl_recursive_return_record.id = vhdl_recursive_return;
    vhdl_recursive_return_record.value = ExpressionId::from_index(23);
    design.mutable_vhdl().mutable_statements().push_back(
        vhdl_recursive_return_record);
    auto vhdl_recursive_function_record = vhdl_function_record;
    vhdl_recursive_function_record.id = vhdl_recursive_function;
    vhdl_recursive_function_record.name = "recursive_constant";
    vhdl_recursive_function_record.callable->formals.clear();
    vhdl_recursive_function_record.statements = { vhdl_recursive_return };
    design.mutable_vhdl().mutable_declarations().push_back(
        vhdl_recursive_function_record);
    design.mutable_vhdl().mutable_units().front()
        .declarations.push_back(vhdl_recursive_function);

    const auto vhdl_type_declaration
        = design.mutable_semantics().add_declaration(
            vhdl_scope, DeclarationKind::type, "folded_range_t",
            sv_add_source, sv_add_origin);
    TypeReference vhdl_semantic_type;
    vhdl_semantic_type.source = sv_add_source;
    vhdl_semantic_type.spelling = "integer";
    const auto vhdl_type = design.mutable_semantics().add_type(
        vhdl_scope, TypeKind::declaration, "folded_range_t",
        vhdl_semantic_type, sv_add_source, sv_add_origin);
    vhdl::TypeDefinition vhdl_type_record;
    vhdl_type_record.id = vhdl_type;
    vhdl_type_record.declaration = vhdl_type_declaration;
    vhdl_type_record.name = "folded_range_t";
    vhdl_type_record.base.type_mark.source = sv_add_source;
    vhdl_type_record.base.type_mark.spelling = "integer";
    vhdl_type_record.source = sv_add_source;
    vhdl_type_record.origin = sv_add_origin;
    vhdl::RangeConstraint vhdl_range;
    vhdl_range.kind = vhdl::RangeKind::integer;
    vhdl_range.left_expression = ExpressionId::from_index(2);
    vhdl_range.right_expression = ExpressionId::from_index(1);
    vhdl_range.source = sv_add_source;
    vhdl_type_record.base.constraints.push_back(vhdl_range);
    design.mutable_vhdl().mutable_types().push_back(
        std::move(vhdl_type_record));
    vhdl::Declaration vhdl_type_declaration_record;
    vhdl_type_declaration_record.id = vhdl_type_declaration;
    vhdl_type_declaration_record.scope = vhdl_scope;
    vhdl_type_declaration_record.form = vhdl::DeclarationForm::type;
    vhdl_type_declaration_record.name = "folded_range_t";
    vhdl_type_declaration_record.source = sv_add_source;
    vhdl_type_declaration_record.origin = sv_add_origin;
    vhdl_type_declaration_record.declared_type = vhdl_type;
    design.mutable_vhdl().mutable_declarations().push_back(
        std::move(vhdl_type_declaration_record));
    design.mutable_vhdl().mutable_units().front()
        .declarations.push_back(vhdl_type_declaration);

    auto deterministic = design;
    assert(normalize_compiled_design(design));
    assert(normalize_compiled_design(deterministic));
    assert_same_expressions(
        design.systemverilog_hir.expressions(),
        deterministic.systemverilog_hir.expressions());
    assert_same_expressions(
        design.vhdl_hir.expressions(),
        deterministic.vhdl_hir.expressions());

    const auto& normalized_sv = design.systemverilog_hir.expressions();
    assert(normalized_sv[2].kind == sv::ExpressionKind::integer_literal);
    assert(normalized_sv[2].text == "12");
    assert(normalized_sv[2].source == sv_add_source);
    assert(normalized_sv[2].origin == sv_add_origin);
    assert(normalized_sv[2].operands.empty());
    assert(normalized_sv[3].text == "-12");
    assert(normalized_sv[6].kind == sv::ExpressionKind::boolean_literal);
    assert(normalized_sv[6].text == "false");
    assert(normalized_sv[7].text == "true");
    assert(normalized_sv[8].text == "true");
    assert(normalized_sv[10].kind == sv::ExpressionKind::binary);
    assert(!normalized_sv[10].folded);
    assert(normalized_sv[13].kind == sv::ExpressionKind::binary);
    assert(!normalized_sv[13].folded);
    assert(normalized_sv[14].kind == sv::ExpressionKind::binary);
    assert(!normalized_sv[14].folded);
    assert(normalized_sv[15].kind == sv::ExpressionKind::binary);
    assert(!normalized_sv[15].folded);
    assert(normalized_sv[15].dependencies.hierarchy);
    assert(normalized_sv[16].text == "35");
    assert(normalized_sv[17].text == "2");
    assert(normalized_sv[18].text == "2");
    assert(normalized_sv[19].text == "true");
    assert(normalized_sv[20].kind == sv::ExpressionKind::integer_literal);
    assert(normalized_sv[20].text == "12");
    assert(normalized_sv[20].source == sv_add_source);
    assert(normalized_sv[20].origin == sv_add_origin);
    assert(normalized_sv[20].dependencies == ResidualDependencies { });
    assert(normalized_sv[21].kind == sv::ExpressionKind::name);
    assert(!normalized_sv[21].folded);
    assert(normalized_sv[21].dependencies.parameters
        == std::vector<DeclarationId> { sv_parameter });
    const auto normalized_sv_parameter = std::ranges::find(
        design.systemverilog_hir.declarations(), sv_parameter,
        &sv::Declaration::id);
    assert(normalized_sv_parameter
        != design.systemverilog_hir.declarations().end());
    assert(normalized_sv_parameter->initializer
        == ExpressionId::from_index(2));
    assert(normalized_sv[
        normalized_sv_parameter->initializer->value()].folded);
    assert(normalized_sv[23].kind
        == sv::ExpressionKind::assignment_pattern);
    assert(normalized_sv[23].folded);
    assert(!normalized_sv[25].folded);
    assert(normalized_sv[25].dependencies.hierarchy);
    assert(!normalized_sv[26].folded);
    assert(normalized_sv[26].dependencies.hierarchy);
    const auto& normalized_sv_range
        = design.systemverilog_hir.types().front().base.packed_range;
    assert(normalized_sv_range);
    assert(normalized_sv_range->left == 12);
    assert(normalized_sv_range->right == 5);
    assert(normalized_sv_range->left_expression
        == ExpressionId::from_index(2));
    assert(normalized_sv_range->right_expression
        == ExpressionId::from_index(1));
    assert(normalized_sv_range->source == sv_add_source);

    const auto& normalized_vhdl = design.vhdl_hir.expressions();
    assert(normalized_vhdl[2].text == "1");
    assert(normalized_vhdl[3].text == "-2");
    assert(normalized_vhdl[4].text == "-15");
    assert(normalized_vhdl[5].text == "-1");
    assert(normalized_vhdl[6].text == "true");
    assert(normalized_vhdl[9].text == "true");
    assert(normalized_vhdl[10].text == "true");
    assert(normalized_vhdl[11].text == "false");
    assert(normalized_vhdl[12].text == "5");
    assert(normalized_vhdl[13].kind == vhdl::ExpressionKind::binary);
    assert(!normalized_vhdl[13].folded);
    assert(normalized_vhdl[14].kind
        == vhdl::ExpressionKind::integer_literal);
    assert(normalized_vhdl[14].text == "1");
    assert(normalized_vhdl[14].source == sv_add_source);
    assert(normalized_vhdl[14].origin == sv_add_origin);
    assert(normalized_vhdl[17].kind
        == vhdl::ExpressionKind::integer_literal);
    assert(normalized_vhdl[17].text == "6");
    assert(normalized_vhdl[17].source == sv_add_source);
    assert(normalized_vhdl[17].origin == sv_add_origin);
    const auto normalized_vhdl_formal = std::ranges::find(
        design.vhdl_hir.declarations(), vhdl_formal,
        &vhdl::Declaration::id);
    assert(normalized_vhdl_formal
        != design.vhdl_hir.declarations().end());
    assert(normalized_vhdl_formal->initializer
        == ExpressionId::from_index(1));
    assert(normalized_vhdl[18].kind == vhdl::ExpressionKind::call);
    assert(!normalized_vhdl[18].folded);
    assert(normalized_vhdl[20].kind == vhdl::ExpressionKind::aggregate);
    assert(normalized_vhdl[20].folded);
    assert(design.vhdl_hir.declarations()[4].initializer
        == ExpressionId::from_index(20));
    assert(!normalized_vhdl[22].folded);
    assert(normalized_vhdl[22].dependencies.hierarchy);
    assert(normalized_vhdl[23].kind == vhdl::ExpressionKind::call);
    assert(!normalized_vhdl[23].folded);
    const auto& normalized_vhdl_range
        = design.vhdl_hir.types().front().base.constraints.front();
    assert(normalized_vhdl_range.left == 1);
    assert(normalized_vhdl_range.right == 3);
    assert(normalized_vhdl_range.left_expression
        == ExpressionId::from_index(2));
    assert(normalized_vhdl_range.right_expression
        == ExpressionId::from_index(1));
    assert(normalized_vhdl_range.source == sv_add_source);

    const auto normalized_once = design;
    assert(normalize_compiled_design(design));
    assert_same_expressions(
        design.systemverilog_hir.expressions(),
        normalized_once.systemverilog_hir.expressions());
    assert_same_expressions(
        design.vhdl_hir.expressions(),
        normalized_once.vhdl_hir.expressions());
    assert(design.systemverilog_hir.types().front().base.packed_range
        == normalized_once.systemverilog_hir.types().front()
               .base.packed_range);
    assert(design.vhdl_hir.types().front().base.constraints
        == normalized_once.vhdl_hir.types().front().base.constraints);

    auto invalid = make_compiled_design();
    invalid.mutable_systemverilog().mutable_expressions().push_back(
        systemverilog_expression(
            invalid, 0, sv::ExpressionKind::binary, "+",
            { ExpressionId::from_index(99) }));
    assert(!normalize_compiled_design(invalid));
}

void test_compiled_design_indexed_lookup_contract()
{
    LinkBundleBuilder builder { "indexed-lookup" };
    const auto systemverilog_unit = builder.add_systemverilog_unit(
        UnitKind::verilog_module, sv::UnitKind::module, "indexed_sv");
    builder.add_systemverilog_graph(
        systemverilog_unit, "indexed_vhdl", "17");
    const auto vhdl_unit = builder.add_vhdl_unit(
        UnitKind::vhdl_entity, vhdl::UnitKind::entity, "indexed_vhdl");
    builder.add_vhdl_graph(vhdl_unit, "indexed_sv", "23");
    auto design = builder.finish();
    const auto validated = validate_compiled_design(design);
    assert(validated);
    assert(&validated->design() == &design);

    const auto& sv_declaration = design.systemverilog_hir.declarations().front();
    const auto& sv_type = design.systemverilog_hir.types().front();
    const auto& sv_expression = design.systemverilog_hir.expressions().front();
    const auto& sv_statement = design.systemverilog_hir.statements().front();
    const auto& sv_process = design.systemverilog_hir.processes().front();
    const auto& sv_instance = design.systemverilog_hir.instances().front();
    const auto& vhdl_declaration = design.vhdl_hir.declarations().front();
    const auto& vhdl_type = design.vhdl_hir.types().front();
    const auto& vhdl_expression = design.vhdl_hir.expressions().front();
    const auto& vhdl_statement = design.vhdl_hir.statements().front();
    const auto& vhdl_process = design.vhdl_hir.processes().front();
    const auto& vhdl_instance = design.vhdl_hir.instances().front();

    const auto assert_systemverilog_records
        = [&](const SpecializedHirUnit& specialized,
              const CompiledDesign& owner) {
            assert(specialized.find_declaration(sv_declaration.id)
                    ->systemverilog
                == &owner.systemverilog_hir.declarations().front());
            assert(specialized.find_type(sv_type.id)->systemverilog
                == &owner.systemverilog_hir.types().front());
            assert(specialized.find_expression(sv_expression.id)
                    ->systemverilog
                == &owner.systemverilog_hir.expressions().front());
            assert(specialized.find_statement(sv_statement.id)
                    ->systemverilog
                == &owner.systemverilog_hir.statements().front());
            assert(specialized.find_process(sv_process.id)->systemverilog
                == &owner.systemverilog_hir.processes().front());
            assert(specialized.find_instance(sv_instance.id)
                    ->systemverilog
                == &owner.systemverilog_hir.instances().front());
        };
    const auto assert_vhdl_records
        = [&](const SpecializedHirUnit& specialized,
              const CompiledDesign& owner) {
            assert(specialized.find_declaration(vhdl_declaration.id)->vhdl
                == &owner.vhdl_hir.declarations().front());
            assert(specialized.find_type(vhdl_type.id)->vhdl
                == &owner.vhdl_hir.types().front());
            assert(specialized.find_expression(vhdl_expression.id)->vhdl
                == &owner.vhdl_hir.expressions().front());
            assert(specialized.find_statement(vhdl_statement.id)->vhdl
                == &owner.vhdl_hir.statements().front());
            assert(specialized.find_process(vhdl_process.id)->vhdl
                == &owner.vhdl_hir.processes().front());
            assert(specialized.find_instance(vhdl_instance.id)->vhdl
                == &owner.vhdl_hir.instances().front());
        };

    const auto validated_systemverilog = make_specialized_hir_unit(
        *validated, systemverilog_unit,
        std::span<const SpecializedHirActualIdentity> { });
    const auto validated_vhdl = make_specialized_hir_unit(
        *validated, vhdl_unit,
        std::span<const SpecializedHirActualIdentity> { });
    assert(validated_systemverilog);
    assert(validated_vhdl);
    assert_systemverilog_records(*validated_systemverilog, design);
    assert_vhdl_records(*validated_vhdl, design);
    const auto hierarchy_specialization
        = validated_systemverilog->with_hierarchy_identities(
            std::span<const SpecializedHirNamedIdentity> { });
    const auto local_specialization
        = validated_vhdl->with_local_actual_identities(
            std::span<const SpecializedHirActualIdentity> { });
    assert_systemverilog_records(hierarchy_specialization, design);
    assert_vhdl_records(local_specialization, design);

    auto annotated = design;
    std::vector<CompiledVhdlExpressionAnnotation> annotations;
    annotations.reserve(annotated.vhdl_hir.expressions().size());
    for (const auto& expression : annotated.vhdl_hir.expressions()) {
        CompiledVhdlExpressionAnnotation annotation;
        annotation.expression = expression.id;
        if (expression.id == vhdl_expression.id) {
            annotation.builtin_operator
                = vhdl::BuiltinOperatorIdentity::
                    ieee_std_logic_1164_not;
        }
        if (expression.referenced_name) {
            annotation.name_resolution.emplace(
                CompiledVhdlExpressionNameResolution {
                    expression.referenced_name->selected,
                    expression.referenced_name->overloads });
        }
        annotations.push_back(std::move(annotation));
    }
    assert(annotated.apply_linked_vhdl_expression_annotations(annotations));
    const auto annotated_expression
        = annotated.find_expression(vhdl_expression.id);
    assert(annotated_expression
        && annotated_expression->vhdl
            == &annotated.vhdl_hir.expressions().front());
    assert(annotated_expression->vhdl->builtin_operator
        == vhdl::BuiltinOperatorIdentity::ieee_std_logic_1164_not);
    assert(annotated.vhdl_linked_imports(vhdl_unit));

    auto stale = design;
    stale.mutable_vhdl().mutable_units().front().standard = "2019";
    assert(!validate_compiled_design(stale));
    const auto stale_systemverilog = make_specialized_hir_unit(
        stale, systemverilog_unit,
        std::span<const SpecializedHirActualIdentity> { });
    const auto stale_vhdl = make_specialized_hir_unit(
        stale, vhdl_unit,
        std::span<const SpecializedHirActualIdentity> { });
    assert(stale_systemverilog);
    assert(stale_vhdl);
    assert_systemverilog_records(*stale_systemverilog, stale);
    assert_vhdl_records(*stale_vhdl, stale);

    assert(design.find_declaration(sv_declaration.id)->systemverilog
        == &sv_declaration);
    assert(design.find_type(sv_type.id)->systemverilog == &sv_type);
    assert(design.find_expression(sv_expression.id)->systemverilog
        == &sv_expression);
    assert(design.find_statement(sv_statement.id)->systemverilog
        == &sv_statement);
    assert(design.find_process(sv_process.id)->systemverilog == &sv_process);
    assert(design.find_instance(sv_instance.id)->systemverilog
        == &sv_instance);
    assert(design.find_declaration(vhdl_declaration.id)->vhdl
        == &vhdl_declaration);
    assert(design.find_type(vhdl_type.id)->vhdl == &vhdl_type);
    assert(design.find_expression(vhdl_expression.id)->vhdl
        == &vhdl_expression);
    assert(design.find_statement(vhdl_statement.id)->vhdl
        == &vhdl_statement);
    assert(design.find_process(vhdl_process.id)->vhdl == &vhdl_process);
    assert(design.find_instance(vhdl_instance.id)->vhdl == &vhdl_instance);
    const auto scoped_vhdl = design.vhdl_declarations_in_scope(
        vhdl_declaration.scope);
    assert(scoped_vhdl);
    assert(std::ranges::find(*scoped_vhdl, vhdl_declaration.id)
        != scoped_vhdl->end());
    assert(!design.vhdl_declarations_in_scope(ScopeId { }));
    assert(!design.vhdl_declarations_in_scope(
        ScopeId::from_index(1000)));

    assert(!design.find_declaration(DeclarationId { }));
    assert(!design.find_declaration(DeclarationId::from_index(1000)));
    assert(!design.find_type(TypeId { }));
    assert(!design.find_type(TypeId::from_index(1000)));
    assert(!design.find_expression(ExpressionId { }));
    assert(!design.find_expression(ExpressionId::from_index(1000)));
    assert(!design.find_statement(StatementId { }));
    assert(!design.find_statement(StatementId::from_index(1000)));
    assert(!design.find_process(ProcessId { }));
    assert(!design.find_process(ProcessId::from_index(1000)));
    assert(!design.find_instance(InstanceId { }));
    assert(!design.find_instance(InstanceId::from_index(1000)));

    auto duplicate = design;
    const auto duplicate_id = duplicate.systemverilog_hir.expressions()
                                  .front()
                                  .id;
    assert(duplicate.find_expression(duplicate_id));
    duplicate.mutable_systemverilog().mutable_expressions().push_back(
        duplicate.systemverilog_hir.expressions().front());
    assert(!duplicate.find_expression(duplicate_id));

    auto cross_language_duplicate = design;
    const auto sv_expression_id = cross_language_duplicate
                                      .systemverilog_hir.expressions()
                                      .front()
                                      .id;
    const auto vhdl_expression_id = cross_language_duplicate
                                        .vhdl_hir.expressions()
                                        .front()
                                        .id;
    assert(cross_language_duplicate.find_expression(sv_expression_id));
    assert(cross_language_duplicate.find_expression(vhdl_expression_id));
    cross_language_duplicate.mutable_vhdl().mutable_expressions().front().id
        = sv_expression_id;
    assert(!cross_language_duplicate.find_expression(sv_expression_id));
    assert(!cross_language_duplicate.find_expression(vhdl_expression_id));

    auto appended = design;
    const auto source_expression = appended.systemverilog_hir.expressions()
                                       .front();
    const auto appended_id = appended.mutable_semantics()
                                 .add_expression_identity(
                                     source_expression.scope,
                                     source_expression.source,
                                     source_expression.origin);
    auto appended_expression = source_expression;
    appended_expression.id = appended_id;
    appended_expression.text = "29";
    appended.mutable_systemverilog().mutable_expressions().push_back(
        std::move(appended_expression));
    const auto appended_view = appended.find_expression(appended_id);
    assert(appended_view && appended_view->systemverilog != nullptr);
    assert(appended_view->systemverilog->text == "29");
    appended.refresh_lookup_indexes();
    assert(appended.find_expression(appended_id)->systemverilog
        == &appended.systemverilog_hir.expressions().back());

    auto copied = design;
    const auto copied_id = copied.systemverilog_hir.declarations().front().id;
    const auto copied_vhdl_scope = vhdl_declaration.scope;
    assert(copied.vhdl_declarations_in_scope(copied_vhdl_scope));
    assert(copied.find_declaration(copied_id)->systemverilog
        == &copied.systemverilog_hir.declarations().front());
    CompiledDesign copy_assigned;
    copy_assigned = design;
    assert(copy_assigned.vhdl_declarations_in_scope(
        copied_vhdl_scope));
    assert(copy_assigned.find_declaration(copied_id)->systemverilog
        == &copy_assigned.systemverilog_hir.declarations().front());
    design = CompiledDesign { };
    assert(copied.find_declaration(copied_id)->systemverilog
        == &copied.systemverilog_hir.declarations().front());
    auto moved = std::move(copied);
    assert(moved.vhdl_declarations_in_scope(copied_vhdl_scope));
    assert(moved.find_declaration(copied_id)->systemverilog
        == &moved.systemverilog_hir.declarations().front());
    CompiledDesign move_assigned;
    move_assigned = std::move(copy_assigned);
    assert(move_assigned.vhdl_declarations_in_scope(
        copied_vhdl_scope));
    assert(move_assigned.find_declaration(copied_id)->systemverilog
        == &move_assigned.systemverilog_hir.declarations().front());
}

struct VhdlPackageMemberIndexFixture {
    CompiledDesign design;
    UnitId package;
    ScopeId scope;
    DeclarationId direct_member;
    DeclarationId extended_member;
    DeclarationId enumeration_literal;
    DeclarationId physical_unit;
};

VhdlPackageMemberIndexFixture make_vhdl_package_member_index_fixture()
{
    LinkBundleBuilder builder { "package-member-index.vhd" };
    VhdlPackageMemberIndexFixture fixture;
    fixture.package = builder.add_vhdl_unit(UnitKind::vhdl_package,
        vhdl::UnitKind::package, "Package_Index", {}, "MiXeD_Lib");
    fixture.scope = builder.model.units()[fixture.package.value()].scope;

    const auto add_declaration
        = [&](const DeclarationKind kind,
              const vhdl::DeclarationForm form,
              std::string name, const bool package_member) {
        const auto id = builder.model.add_declaration(fixture.scope,
            kind, name, builder.source, builder.origin);
        vhdl::Declaration declaration;
        declaration.id = id;
        declaration.scope = fixture.scope;
        declaration.form = form;
        declaration.name = std::move(name);
        declaration.source = builder.source;
        declaration.origin = builder.origin;
        builder.vhdl_hir.mutable_declarations().push_back(
            std::move(declaration));
        if (package_member) {
            builder.vhdl_unit(fixture.package).declarations.push_back(id);
        }
        return id;
    };
    fixture.direct_member = add_declaration(DeclarationKind::constant,
        vhdl::DeclarationForm::constant, "Member_Name", true);
    fixture.extended_member = add_declaration(DeclarationKind::constant,
        vhdl::DeclarationForm::constant, "\\Case_Sensitive\\", true);

    const auto add_type = [&](std::string name,
                              const vhdl::TypeForm form) {
        const auto declaration = add_declaration(DeclarationKind::type,
            vhdl::DeclarationForm::type, name, true);
        TypeReference reference;
        reference.source = builder.source;
        reference.spelling = name;
        const auto type = builder.model.add_type(fixture.scope,
            TypeKind::declaration, name, reference,
            builder.source, builder.origin);
        const auto found = std::ranges::find(
            builder.vhdl_hir.mutable_declarations(), declaration,
            &vhdl::Declaration::id);
        assert(found != builder.vhdl_hir.declarations().end());
        found->declared_type = type;
        vhdl::TypeDefinition definition;
        definition.id = type;
        definition.declaration = declaration;
        definition.form = form;
        definition.name = std::move(name);
        definition.source = builder.source;
        definition.origin = builder.origin;
        builder.vhdl_hir.mutable_types().push_back(std::move(definition));
        return type;
    };

    const auto enumeration_type = add_type(
        "State_Type", vhdl::TypeForm::enumeration);
    fixture.enumeration_literal = add_declaration(
        DeclarationKind::enumeration_literal,
        vhdl::DeclarationForm::enumeration_literal,
        "Ready_State", false);
    const auto enumeration = std::ranges::find(
        builder.vhdl_hir.mutable_types(), enumeration_type,
        &vhdl::TypeDefinition::id);
    assert(enumeration != builder.vhdl_hir.types().end());
    enumeration->enumeration_literals.push_back({
        fixture.enumeration_literal, "Ready_State", 0U, builder.source });

    const auto physical_type = add_type(
        "Delay_Type", vhdl::TypeForm::physical);
    fixture.physical_unit = add_declaration(DeclarationKind::constant,
        vhdl::DeclarationForm::constant, "Nano_Second", false);
    const auto physical = std::ranges::find(
        builder.vhdl_hir.mutable_types(), physical_type,
        &vhdl::TypeDefinition::id);
    assert(physical != builder.vhdl_hir.types().end());
    physical->physical_units.push_back({ fixture.physical_unit,
        "Nano_Second", std::nullopt, 1, builder.source });

    fixture.design = builder.finish();
    return fixture;
}

void test_compiled_vhdl_predefined_subtype_fast_path()
{
    LinkBundleBuilder builder { "resolver-predefined-subtype.vhd" };
    const auto unit = builder.add_vhdl_unit(UnitKind::vhdl_entity,
        vhdl::UnitKind::entity, "predefined_subtype");
    auto design = builder.finish();
    assert(design.valid());

    const std::array<SpecializedHirActualIdentity, 0> actuals { };
    const auto specialized = make_specialized_hir_unit(
        design, unit, actuals);
    assert(specialized);
    const CompiledDesignResolver canonical { design, unit };
    const CompiledDesignResolver optimized { *specialized };
    const auto assert_equivalent = [&](const vhdl::SubtypeIndication& input,
                                       const ScopeId use_scope = ScopeId { }) {
        const auto expected = canonical.effective_vhdl_subtype(
            input, use_scope);
        const auto actual = optimized.effective_vhdl_subtype(
            input, use_scope);
        assert(expected == actual);
        return actual;
    };

    vhdl::SubtypeIndication scalar;
    scalar.type_mark.spelling = "std_logic";
    const auto effective_scalar = assert_equivalent(scalar);
    assert(effective_scalar
        && effective_scalar->domain == vhdl::ValueDomain::logic9
        && effective_scalar->executable_width == 1U);
    assert_equivalent(
        scalar, design.units()[unit.value()].scope);

    vhdl::SubtypeIndication vector;
    vector.type_mark.spelling = "unsigned";
    vhdl::RangeConstraint range;
    range.kind = vhdl::RangeKind::array_index;
    range.left = 7;
    range.right = 0;
    range.descending = true;
    vector.constraints.push_back(range);
    const auto effective_vector = assert_equivalent(vector);
    assert(effective_vector
        && effective_vector->domain == vhdl::ValueDomain::logic9
        && effective_vector->executable_width == 8U
        && !effective_vector->unconstrained);

    auto residual = vector;
    residual.constraints.front().left_expression
        = ExpressionId::from_index(1000U);
    assert_equivalent(residual);

    auto unconstrained = vector;
    unconstrained.constraints.clear();
    unconstrained.unconstrained = true;
    assert_equivalent(unconstrained);

    auto attributed = vector;
    attributed.predefined_attribute
        = vhdl::PredefinedAttribute::index;
    assert_equivalent(attributed);

    auto unknown = scalar;
    unknown.type_mark.spelling = "user_defined";
    assert_equivalent(unknown);
}

void test_vhdl_builtin_std_logic_1164_provenance()
{
    struct LinkedFixture {
        CompiledDesign design;
        UnitId architecture;
        std::optional<TypeId> user_vector_type;
    };
    const auto make_fixture = [](const std::string& source_name,
                                  const bool import_user_operator,
                                  const bool import_std_logic_1164,
                                  const bool define_user_vector_type) {
        LinkBundleBuilder builder { source_name };
        builder.add_vhdl_unit(UnitKind::vhdl_entity,
            vhdl::UnitKind::entity, "provenance_entity");
        const auto architecture = builder.add_vhdl_unit(
            UnitKind::vhdl_architecture, vhdl::UnitKind::architecture,
            "rtl", "provenance_entity");
        const auto scope
            = builder.model.units()[architecture.value()].scope;

        const auto add_use = [&](const std::string_view package) {
            vhdl::ContextItem clause;
            clause.kind = vhdl::ContextKind::use_clause;
            clause.source = builder.source;
            clause.selected_names.push_back(vhdl_name(
                std::string { package } + ".all", builder.source));
            builder.vhdl_unit(architecture).context.push_back(
                std::move(clause));
        };
        if (import_std_logic_1164) {
            add_use("ieee.std_logic_1164");
        }
        if (import_user_operator) {
            const auto package = builder.add_vhdl_unit(
                UnitKind::vhdl_package, vhdl::UnitKind::package,
                "user_ops");
            const auto package_scope
                = builder.model.units()[package.value()].scope;
            const auto declaration = builder.model.add_declaration(
                package_scope, DeclarationKind::function, "and",
                builder.source, builder.origin);
            vhdl::Declaration function;
            function.id = declaration;
            function.scope = package_scope;
            function.form = vhdl::DeclarationForm::function;
            function.name = "and";
            function.source = builder.source;
            function.origin = builder.origin;
            builder.vhdl_hir.mutable_declarations().push_back(
                std::move(function));
            builder.vhdl_unit(package).declarations.push_back(declaration);
            add_use("work.user_ops");
        }

        std::optional<TypeId> user_vector_type;
        if (define_user_vector_type) {
            const auto declaration = builder.model.add_declaration(
                scope, DeclarationKind::type, "std_logic_vector",
                builder.source, builder.origin);
            TypeReference base;
            base.source = builder.source;
            base.spelling = "std_logic_vector";
            const auto type = builder.model.add_type(scope,
                TypeKind::declaration, "std_logic_vector", base,
                builder.source, builder.origin);
            vhdl::Declaration type_declaration;
            type_declaration.id = declaration;
            type_declaration.scope = scope;
            type_declaration.form = vhdl::DeclarationForm::type;
            type_declaration.name = "std_logic_vector";
            type_declaration.declared_type = type;
            type_declaration.source = builder.source;
            type_declaration.origin = builder.origin;
            builder.vhdl_hir.mutable_declarations().push_back(
                std::move(type_declaration));
            vhdl::TypeDefinition definition;
            definition.id = type;
            definition.declaration = declaration;
            definition.form = vhdl::TypeForm::array;
            definition.name = "std_logic_vector";
            definition.element_subtype.emplace();
            definition.element_subtype->type_mark.spelling = "std_logic";
            definition.element_subtype->type_mark.source = builder.source;
            definition.element_subtype->domain = vhdl::ValueDomain::logic9;
            definition.element_subtype->executable_width = 1U;
            vhdl::ArrayDimension dimension;
            dimension.index_subtype = vhdl_name("natural", builder.source);
            dimension.unconstrained = true;
            dimension.source = builder.source;
            definition.array_dimensions.push_back(std::move(dimension));
            definition.source = builder.source;
            definition.origin = builder.origin;
            builder.vhdl_hir.mutable_types().push_back(
                std::move(definition));
            builder.vhdl_unit(architecture).declarations.push_back(
                declaration);
            user_vector_type = type;
        }

        vhdl::SubtypeIndication vector_subtype;
        vector_subtype.type_mark.spelling = "std_logic_vector";
        vector_subtype.type_mark.source = builder.source;
        vector_subtype.domain = vhdl::ValueDomain::logic9;
        vector_subtype.executable_width = 8U;
        const auto add_signal = [&](const std::string& name) {
            const auto declaration = builder.model.add_declaration(scope,
                DeclarationKind::signal, name,
                builder.source, builder.origin);
            vhdl::Declaration signal;
            signal.id = declaration;
            signal.scope = scope;
            signal.form = vhdl::DeclarationForm::signal;
            signal.name = name;
            signal.source = builder.source;
            signal.origin = builder.origin;
            signal.subtype = vector_subtype;
            signal.object_class = vhdl::ObjectClass::signal;
            builder.vhdl_hir.mutable_declarations().push_back(
                std::move(signal));
            builder.vhdl_unit(architecture).declarations.push_back(
                declaration);
        };
        add_signal("source_a");
        add_signal("source_b");

        const auto add_expression = [&](const vhdl::ExpressionKind kind,
                                        const std::string& text,
                                        std::vector<ExpressionId> operands) {
            const auto expression_id = builder.model.add_expression_identity(
                scope, builder.source, builder.origin);
            vhdl::Expression expression;
            expression.id = expression_id;
            expression.scope = scope;
            expression.kind = kind;
            expression.text = text;
            expression.source = builder.source;
            expression.origin = builder.origin;
            expression.operands = std::move(operands);
            expression.referenced_name = vhdl_name(text, builder.source);
            builder.vhdl_hir.mutable_expressions().push_back(
                std::move(expression));
            return expression_id;
        };
        const auto left = add_expression(
            vhdl::ExpressionKind::name, "source_a", { });
        const auto right = add_expression(
            vhdl::ExpressionKind::name, "source_b", { });
        const auto inverse = add_expression(
            vhdl::ExpressionKind::unary, "not", { right });
        add_expression(vhdl::ExpressionKind::binary,
            "and", { left, inverse });

        std::vector<CompiledDesign> inputs;
        inputs.push_back(builder.finish());
        auto linked = link_compiled_designs(std::move(inputs));
        assert(linked.ok());
        return LinkedFixture {
            std::move(*linked.design), architecture, user_vector_type
        };
    };

    const auto find_operator = [](const CompiledDesign& design,
                                  const vhdl::ExpressionKind kind,
                                  const std::string_view spelling)
        -> const vhdl::Expression& {
        const auto expression = std::ranges::find_if(
            design.vhdl_hir.expressions(), [&](const auto& candidate) {
                return candidate.kind == kind
                    && candidate.text == spelling;
            });
        assert(expression != design.vhdl_hir.expressions().end());
        return *expression;
    };
    const auto assert_indexed_operator = [&](const CompiledDesign& design,
                                             const vhdl::ExpressionKind kind,
                                             const std::string_view spelling,
                                             const vhdl::BuiltinOperatorIdentity
                                                 expected_operator,
                                             const std::optional<DeclarationId>
                                                 expected_declaration) {
        const auto& expression = find_operator(design, kind, spelling);
        const auto indexed = design.find_expression(expression.id);
        assert(indexed && indexed->vhdl == &expression);
        assert(indexed->vhdl->builtin_operator == expected_operator);
        if (!expected_declaration) {
            return;
        }
        assert(indexed->vhdl->referenced_name);
        const auto& name = *indexed->vhdl->referenced_name;
        assert(name.selected == expected_declaration
            || std::ranges::find(
                   name.overloads, *expected_declaration)
                != name.overloads.end());
    };
    const auto imported = make_fixture(
        "builtin-vector-import.vhd", false, true, false);
    assert(find_operator(imported.design,
               vhdl::ExpressionKind::unary, "not").builtin_operator
        == vhdl::BuiltinOperatorIdentity::ieee_std_logic_1164_not);
    assert(find_operator(imported.design,
               vhdl::ExpressionKind::binary, "and").builtin_operator
        == vhdl::BuiltinOperatorIdentity::ieee_std_logic_1164_and);
    assert_indexed_operator(imported.design,
        vhdl::ExpressionKind::unary, "not",
        vhdl::BuiltinOperatorIdentity::ieee_std_logic_1164_not,
        std::nullopt);
    assert_indexed_operator(imported.design,
        vhdl::ExpressionKind::binary, "and",
        vhdl::BuiltinOperatorIdentity::ieee_std_logic_1164_and,
        std::nullopt);
    vhdl::SubtypeIndication imported_vector;
    imported_vector.type_mark.spelling = "std_logic_vector";
    imported_vector.domain = vhdl::ValueDomain::logic9;
    imported_vector.executable_width = 8U;
    const CompiledDesignResolver imported_resolver {
        imported.design, imported.architecture
    };
    const auto imported_subtype = imported_resolver.effective_vhdl_subtype(
        imported_vector,
        imported.design.units()[imported.architecture.value()].scope);
    assert(imported_subtype
        && imported_subtype->builtin_type
            == vhdl::BuiltinTypeIdentity::
                ieee_std_logic_1164_std_logic_vector);

    const auto missing_import = make_fixture(
        "builtin-vector-missing-import.vhd", false, false, false);
    const auto missing_imports =
        missing_import.design.vhdl_linked_imports(
            missing_import.architecture);
    assert(missing_imports);
    assert(std::ranges::none_of(*missing_imports,
        [](const CompiledVhdlImport& item) {
            return item.library == "ieee"
                && item.package == "std_logic_1164";
        }));
    const CompiledDesignResolver missing_import_resolver {
        missing_import.design, missing_import.architecture
    };
    const auto missing_import_subtype
        = missing_import_resolver.effective_vhdl_subtype(
            imported_vector,
            missing_import.design.units()[
                missing_import.architecture.value()].scope);
    assert(missing_import_subtype
        && !missing_import_subtype->type_mark.target.valid()
        && missing_import_subtype->builtin_type
            == vhdl::BuiltinTypeIdentity::none);
    assert(find_operator(missing_import.design,
               vhdl::ExpressionKind::unary, "not").builtin_operator
        == vhdl::BuiltinOperatorIdentity::none);
    assert(find_operator(missing_import.design,
               vhdl::ExpressionKind::binary, "and").builtin_operator
        == vhdl::BuiltinOperatorIdentity::none);
    assert_indexed_operator(missing_import.design,
        vhdl::ExpressionKind::binary, "and",
        vhdl::BuiltinOperatorIdentity::none, std::nullopt);

    const auto user_vector_shadow = make_fixture(
        "builtin-vector-user-shadow.vhd", false, true, true);
    assert(user_vector_shadow.user_vector_type);
    const auto shadow_imports
        = user_vector_shadow.design.vhdl_linked_imports(
            user_vector_shadow.architecture);
    assert(shadow_imports);
    assert(std::ranges::any_of(*shadow_imports,
        [](const CompiledVhdlImport& item) {
            return item.library == "ieee"
                && item.package == "std_logic_1164"
                && item.member == "all";
        }));
    const auto shadow_type_declarations
        = user_vector_shadow.design.vhdl_type_declarations_named(
            "std_logic_vector");
    assert(shadow_type_declarations
        && shadow_type_declarations->size() == 1U);
    const auto shadow_type = user_vector_shadow.design.find_type(
        *user_vector_shadow.user_vector_type);
    assert(shadow_type && shadow_type->vhdl != nullptr
        && shadow_type->vhdl->name == "std_logic_vector");
    const CompiledDesignResolver shadow_resolver {
        user_vector_shadow.design, user_vector_shadow.architecture
    };
    const auto shadow_subtype = shadow_resolver.effective_vhdl_subtype(
        imported_vector,
        user_vector_shadow.design.units()[
            user_vector_shadow.architecture.value()].scope);
    assert(shadow_subtype
        && shadow_subtype->type_mark.target
            == *user_vector_shadow.user_vector_type
        && shadow_subtype->builtin_type
            == vhdl::BuiltinTypeIdentity::none);

    const auto overloaded = make_fixture(
        "builtin-vector-user-overload.vhd", true, true, false);
    const auto imports
        = overloaded.design.vhdl_linked_imports(overloaded.architecture);
    assert(imports);
    assert(std::ranges::any_of(*imports, [](const CompiledVhdlImport& item) {
        return item.library == "work" && item.package == "user_ops"
            && item.member == "all";
    }));

    auto reordered_imports = overloaded.design;
    auto& reordered_units =
        reordered_imports.mutable_vhdl().mutable_units();
    const auto reordered_owner = std::ranges::find(
        reordered_units, overloaded.architecture, &vhdl::Unit::id);
    assert(reordered_owner != reordered_units.end());
    assert(!reordered_owner->context.empty());
    auto& duplicate_use = reordered_owner->context.front();
    assert(duplicate_use.kind == vhdl::ContextKind::use_clause);
    assert(!duplicate_use.selected_names.empty());
    duplicate_use.selected_names.push_back(
        duplicate_use.selected_names.front());

    auto& reordered_references = reordered_imports.mutable_references();
    const auto user_ops_reference = std::ranges::find_if(
        reordered_references, [&](const CompiledReference& reference) {
            return reference.kind == CompiledReferenceKind::package
                && reference.owner == overloaded.architecture
                && reference.library == "work"
                && reference.name == "user_ops";
        });
    const auto std_logic_reference = std::ranges::find_if(
        reordered_references, [&](const CompiledReference& reference) {
            return reference.kind == CompiledReferenceKind::package
                && reference.owner == overloaded.architecture
                && reference.library == "ieee"
                && reference.name == "std_logic_1164";
        });
    assert(user_ops_reference != reordered_references.end());
    assert(std_logic_reference != reordered_references.end());
    std::iter_swap(user_ops_reference, std_logic_reference);
    reordered_imports.refresh_lookup_indexes();

    const auto ordered_imports
        = reordered_imports.vhdl_linked_imports(overloaded.architecture);
    assert(ordered_imports && ordered_imports->size() == 2U);
    assert((*ordered_imports)[0].library == "work");
    assert((*ordered_imports)[0].package == "user_ops");
    assert((*ordered_imports)[0].member == "all");
    assert((*ordered_imports)[1].library == "ieee");
    assert((*ordered_imports)[1].package == "std_logic_1164");
    assert((*ordered_imports)[1].member == "all");

    const auto package_members
        = overloaded.design.vhdl_package_members("and");
    assert(package_members);
    const auto user_and = std::ranges::find_if(*package_members,
        [](const CompiledVhdlPackageMemberIndexEntry& member) {
            return member.library == "work"
                && member.package == "user_ops";
        });
    assert(user_and != package_members->end());
    const auto& overloaded_and = find_operator(overloaded.design,
        vhdl::ExpressionKind::binary, "and");
    assert(overloaded_and.referenced_name);
    assert(overloaded_and.referenced_name->selected == user_and->member
        || std::ranges::find(overloaded_and.referenced_name->overloads,
               user_and->member)
            != overloaded_and.referenced_name->overloads.end());
    assert(overloaded_and.builtin_operator
        == vhdl::BuiltinOperatorIdentity::none);
    assert_indexed_operator(overloaded.design,
        vhdl::ExpressionKind::binary, "and",
        vhdl::BuiltinOperatorIdentity::none, user_and->member);
    assert(find_operator(overloaded.design,
               vhdl::ExpressionKind::unary, "not").builtin_operator
        == vhdl::BuiltinOperatorIdentity::ieee_std_logic_1164_not);
}

void test_vhdl_package_member_index_contract()
{
    auto fixture = make_vhdl_package_member_index_fixture();
    const auto named_types
        = fixture.design.vhdl_type_declarations_named("sTaTe_TyPe");
    assert(named_types && named_types->size() == 1U);
    assert(*named_types->begin()
        == fixture.design.vhdl_hir.types().front().declaration);
    const auto missing_type
        = fixture.design.vhdl_type_declarations_named("missing");
    assert(missing_type && missing_type->empty());
    const auto assert_member
        = [&](const CompiledDesign& design,
              const std::string_view spelling,
              const DeclarationId expected) {
        const auto members = design.vhdl_package_members(spelling);
        assert(members && members->size() == 1U);
        const auto& member = members->front();
        assert(member.library == "MiXeD_Lib");
        assert(member.package == "Package_Index");
        assert(member.template_unit == fixture.package);
        assert(member.member == expected);
    };

    assert_member(fixture.design, "mEmBeR_nAmE", fixture.direct_member);
    assert_member(fixture.design, "\\Case_Sensitive\\",
        fixture.extended_member);
    const auto wrong_extended
        = fixture.design.vhdl_package_members("\\case_sensitive\\");
    assert(wrong_extended && wrong_extended->empty());
    assert_member(fixture.design, "READY_STATE",
        fixture.enumeration_literal);
    assert_member(fixture.design, "nano_second", fixture.physical_unit);
    const auto missing = fixture.design.vhdl_package_members("missing");
    assert(missing && missing->empty());

    vhdl::Name qualified_name;
    qualified_name.spelling = "pAcKaGe_InDeX.mEmBeR_nAmE";
    qualified_name.canonical = qualified_name.spelling;
    const CompiledDesignResolver indexed_resolver {
        fixture.design, fixture.package
    };
    const auto indexed = indexed_resolver.resolve_vhdl_package_members(
        qualified_name, fixture.scope).unique();
    assert(indexed && indexed->member == fixture.direct_member);

    auto stale = fixture.design;
    stale.mutable_vhdl().mutable_units().front().standard = "2019";
    assert(!stale.vhdl_package_members("Member_Name"));
    assert(!stale.vhdl_type_declarations_named("State_Type"));
    const CompiledDesignResolver fallback_resolver {
        stale, fixture.package
    };
    const auto fallback = fallback_resolver.resolve_vhdl_package_members(
        qualified_name, fixture.scope).unique();
    assert(fallback && fallback->member == fixture.direct_member);
    stale.refresh_lookup_indexes();
    assert_member(stale, "MEMBER_NAME", fixture.direct_member);

    auto copied = fixture.design;
    CompiledDesign copy_assigned;
    copy_assigned = fixture.design;
    fixture.design = CompiledDesign { };
    assert_member(copied, "member_name", fixture.direct_member);
    assert_member(copied, "\\Case_Sensitive\\",
        fixture.extended_member);
    assert_member(copy_assigned, "ready_state",
        fixture.enumeration_literal);
    assert_member(copy_assigned, "NANO_SECOND", fixture.physical_unit);

    auto moved = std::move(copied);
    CompiledDesign move_assigned;
    move_assigned = std::move(copy_assigned);
    assert_member(moved, "MEMBER_NAME", fixture.direct_member);
    assert_member(moved, "\\Case_Sensitive\\",
        fixture.extended_member);
    assert_member(move_assigned, "READY_STATE",
        fixture.enumeration_literal);
    assert_member(move_assigned, "nano_second", fixture.physical_unit);

    const auto make_vhdl_append_input = [] {
        LinkBundleBuilder builder { "z-appended-package.vhd" };
        const auto package = builder.add_vhdl_unit(
            UnitKind::vhdl_package, vhdl::UnitKind::package,
            "Appended_Package", { }, "MiXeD_Lib");
        const auto scope = builder.model.units()[package.value()].scope;
        for (const std::string_view name : {
                 "Extra_One", "Extra_Two", "Extra_Three", "Extra_Four" }) {
            const auto declaration_id = builder.model.add_declaration(
                scope, DeclarationKind::constant, std::string { name },
                builder.source, builder.origin);
            vhdl::Declaration declaration;
            declaration.id = declaration_id;
            declaration.scope = scope;
            declaration.form = vhdl::DeclarationForm::constant;
            declaration.name = name;
            declaration.source = builder.source;
            declaration.origin = builder.origin;
            builder.vhdl_hir.mutable_declarations().push_back(
                std::move(declaration));
            builder.vhdl_unit(package).declarations.push_back(
                declaration_id);
        }
        return builder.finish();
    };

    auto indexed_input = make_vhdl_package_member_index_fixture();
    auto append_input = make_vhdl_append_input();
    assert(indexed_input.design.vhdl_type_declarations_named("State_Type"));
    assert(indexed_input.design.vhdl_package_members("Member_Name"));
    std::vector<CompiledDesign> link_inputs;
    link_inputs.push_back(std::move(indexed_input.design));
    link_inputs.push_back(std::move(append_input));
    const auto linked = link_compiled_designs(std::move(link_inputs));
    assert(linked.ok() && linked.design);
    const auto linked_types
        = linked.design->vhdl_type_declarations_named("STATE_TYPE");
    assert(linked_types && linked_types->size() == 1U);
    const auto linked_members
        = linked.design->vhdl_package_members("MEMBER_NAME");
    assert(linked_members && linked_members->size() == 1U);
    assert(linked_members->front().package == "Package_Index");
}

} // namespace

void run_compiled_design_tests()
{
    auto design = make_compiled_design();
    assert(design.valid());
    assert(design.units().size() == 12);
    assert(design.systemverilog_units().size() == 6);
    assert(design.vhdl_units().size() == 6);

    assert_systemverilog_lookup(design, UnitKind::verilog_module,
        Language::verilog, "module_unit");
    assert_systemverilog_lookup(design,
        UnitKind::systemverilog_interface,
        Language::system_verilog, "shared_unit");
    assert_systemverilog_lookup(design,
        UnitKind::systemverilog_program,
        Language::system_verilog, "program_unit");
    assert_systemverilog_lookup(design,
        UnitKind::systemverilog_configuration,
        Language::system_verilog, "configuration_unit");
    assert_systemverilog_lookup(design,
        UnitKind::systemverilog_package,
        Language::system_verilog, "shared_unit");
    assert_systemverilog_lookup(design,
        UnitKind::systemverilog_bind,
        Language::system_verilog, "bind_unit");

    assert_vhdl_lookup(design, UnitKind::vhdl_entity, "entity_unit");
    assert_vhdl_lookup(design, UnitKind::vhdl_architecture,
        "entity_unit", "rtl");
    assert_vhdl_lookup(design, UnitKind::vhdl_configuration,
        "vhdl_configuration", "entity_unit");
    assert_vhdl_lookup(design, UnitKind::vhdl_package, "vhdl_package");
    assert_vhdl_lookup(design, UnitKind::vhdl_context, "vhdl_context");
    assert_vhdl_lookup(design,
        UnitKind::vhdl_psl_verification_unit, "vhdl_psl");

    assert(!design.find_unit(UnitId {}));
    assert(!design.find_unit(UnitId::from_index(1000)));
    assert(!design.find_unit(Language::system_verilog,
        "work", "module_unit"));
    assert(design.find_unit(Language::verilog,
        "work", "module_unit"));
    assert(!design.find_unit(Language::system_verilog,
        "work", "shared_unit"));
    assert(design.find_unit(Language::system_verilog,
        "work", "program_unit"));
    assert(!design.find_unit(Language::vhdl,
        "work", "rtl", "entity_unit"));
    assert(!design.find_unit(Language::vhdl,
        "work", "entity_unit", "wrong_architecture"));
    assert(!design.find_unit(UnitKind::vhdl_architecture,
        "work", "entity_unit"));
    const auto module_candidates = design.find_units(
        UnitKind::verilog_module, {}, "module_unit");
    assert(module_candidates.size() == 1);
    assert(module_candidates.front().systemverilog != nullptr);
    assert(module_candidates.front().systemverilog->name == "module_unit");
    assert(design.find_units(
        UnitKind::verilog_module, "other", "module_unit").empty());

    auto duplicate_id = design;
    duplicate_id.mutable_systemverilog().mutable_units().push_back(
        duplicate_id.systemverilog_units().front());
    const auto duplicated_unit = design.systemverilog_units().front().id;
    assert(!duplicate_id.find_unit(duplicated_unit));
    assert(!duplicate_id.valid());

    auto cross_language_duplicate = design;
    cross_language_duplicate.mutable_vhdl().mutable_units().front().id
        = duplicated_unit;
    assert(!cross_language_duplicate.find_unit(duplicated_unit));
    assert(!cross_language_duplicate.valid());

    auto mismatched_identity = design;
    mismatched_identity.mutable_vhdl().mutable_units().front().name
        = "wrong_entity";
    assert(!mismatched_identity.find_unit(
        design.vhdl_units().front().id));
    assert(!mismatched_identity.valid());

    auto independent_copy = design;
    const auto original = design.find_unit(
        UnitKind::systemverilog_package, {}, "shared_unit");
    const auto copied = independent_copy.find_unit(
        UnitKind::systemverilog_package, {}, "shared_unit");
    assert(original && copied);
    assert(original->identity != copied->identity);
    assert(original->systemverilog != copied->systemverilog);
    design = CompiledDesign {};
    assert(independent_copy.find_unit(UnitKind::systemverilog_package,
        "work", "shared_unit"));

    auto moved = std::move(independent_copy);
    assert(moved.find_unit(UnitKind::vhdl_architecture,
        {}, "entity_unit", "rtl"));

    test_compiled_udp_validation();
    test_compiled_design_linker();
    test_compiled_design_relocation_regressions();
    test_systemverilog_auxiliary_record_relocation();
    test_systemverilog_auxiliary_record_projection();
    test_compiled_systemverilog_resolver_exports_and_lets();
    test_compiled_systemverilog_resolver_enum_literal_duplicates();
    test_compiled_systemverilog_resolver_expression_targets();
    test_compiled_systemverilog_resolver_synthetic_type_designator();
    test_compiled_design_specialization();
    test_compiled_design_resolver_generic_callable_profile();
    test_compiled_vhdl_resolver_retained_time_layout();
    test_compiled_vhdl_resolver_array_shapes();
    test_compiled_vhdl_predefined_subtype_fast_path();
    test_vhdl_builtin_std_logic_1164_provenance();
    test_compiled_association_resolution();
    test_specialized_hir_unit();
    test_specialized_hir_parameter_bit_selection();
    test_specialized_hir_vhdl_while_function();
    test_specialized_hir_vhdl_integer_exponentiation();
    test_specialized_hir_vhdl_packed_constant_function();
    test_specialized_hir_vhdl_generated_packed_constant_iterator();
    test_specialized_hir_vhdl_selection_constant_function();
    test_specialized_hir_vhdl_local_array_constant_function();
    test_specialized_hir_vhdl_integer_array_constant_function();
    test_specialized_hir_vhdl_integer_array_rejects_shadowed_integer();
    test_specialized_hir_vhdl_packed_array_projection();
    test_compiled_design_normalization();
    test_compiled_design_indexed_lookup_contract();
    test_vhdl_package_member_index_contract();
}
