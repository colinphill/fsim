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
    test_compiled_systemverilog_resolver_expression_targets();
    test_compiled_systemverilog_resolver_synthetic_type_designator();
    test_compiled_design_specialization();
    test_compiled_design_resolver_generic_callable_profile();
    test_compiled_vhdl_resolver_retained_time_layout();
    test_compiled_vhdl_resolver_array_shapes();
    test_compiled_vhdl_predefined_subtype_fast_path();
    test_compiled_association_resolution();
    test_specialized_hir_unit();
    test_compiled_design_normalization();
    test_compiled_design_indexed_lookup_contract();
    test_vhdl_package_member_index_contract();
}
