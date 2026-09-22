// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/systemverilog_class_specialization.hpp"

#include <algorithm>
#include <cassert>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace fsim::semantic;

struct FixtureBuilder {
    Model model;
    sv::Hir hir;
    SourceSpanId source;
    OriginId unit_origin;
    UnitId unit;

    FixtureBuilder()
    {
        const auto file = model.intern_source_file(
            "classes.sv", "class-specialization-fixture");
        source = model.intern_source_span(
            file, "classes.sv", { 0, 1, 1 }, { 100, 10, 1 });
        unit_origin = model.add_origin(
            OriginKind::parsed, source, std::nullopt, "work::pkg");
        unit = model.add_unit(Language::system_verilog,
            UnitKind::systemverilog_package, "work", "pkg", { },
            source, unit_origin);
        sv::Unit retained;
        retained.id = unit;
        retained.scope = model.units()[unit.value()].scope;
        retained.kind = sv::UnitKind::package;
        retained.library = "work";
        retained.name = "pkg";
        retained.source = source;
        retained.origin = unit_origin;
        retained.compilation_unit_identity = "fixture-unit";
        retained.standard = "2023";
        retained.source_dependencies.push_back("include.svh");
        hir.mutable_units().push_back(std::move(retained));
    }

    sv::ClassDeclaration make_class(
        const std::string& name, const bool interface_class = false)
    {
        sv::ClassDeclaration declaration;
        declaration.name = name;
        declaration.canonical_identity = "work::pkg::" + name;
        declaration.enclosing_identity = "work::pkg";
        declaration.source = source;
        declaration.origin = model.add_origin(OriginKind::parsed,
            source, unit_origin, declaration.canonical_identity);
        declaration.scope = model.add_scope(unit,
            model.units()[unit.value()].scope, name, source,
            declaration.origin);
        declaration.interface_class = interface_class;
        return declaration;
    }

    ExpressionId literal(const ScopeId scope, const OriginId origin,
        std::string text,
        const sv::ExpressionKind kind = sv::ExpressionKind::integer_literal)
    {
        const auto id = model.add_expression_identity(scope, source, origin);
        sv::Expression expression;
        expression.id = id;
        expression.scope = scope;
        expression.kind = kind;
        expression.text = std::move(text);
        expression.source = source;
        expression.origin = origin;
        expression.folded = true;
        hir.mutable_expressions().push_back(std::move(expression));
        return id;
    }

    ExpressionId parameter_name(const sv::ClassDeclaration& owner,
        const DeclarationId parameter, const std::string& name)
    {
        const auto id = model.add_expression_identity(
            owner.scope, source, owner.origin);
        sv::Expression expression;
        expression.id = id;
        expression.scope = owner.scope;
        expression.kind = sv::ExpressionKind::name;
        expression.text = name;
        expression.source = source;
        expression.origin = owner.origin;
        expression.referenced_name = sv::Name {
            name, source, parameter, { parameter }
        };
        hir.mutable_expressions().push_back(std::move(expression));
        return id;
    }

    DeclarationId value_parameter(sv::ClassDeclaration& owner,
        const std::string& name,
        const std::optional<std::string>& default_value)
    {
        const auto origin = model.add_origin(
            OriginKind::parsed, source, owner.origin, name);
        const auto id = model.add_declaration(owner.scope,
            DeclarationKind::constant, name, source, origin);
        sv::Declaration declaration;
        declaration.id = id;
        declaration.scope = owner.scope;
        declaration.form = sv::DeclarationForm::parameter;
        declaration.name = name;
        declaration.source = source;
        declaration.origin = origin;
        if (default_value) {
            declaration.initializer = literal(
                owner.scope, origin, *default_value);
        }
        declaration.type = integral_type(32);
        hir.mutable_declarations().push_back(declaration);
        owner.parameters.push_back({ id, name, false,
            declaration.type, std::nullopt, declaration.initializer,
            source, origin, { } });
        owner.member_declarations.push_back(id);
        return id;
    }

    DeclarationId type_parameter(sv::ClassDeclaration& owner,
        const std::string& name,
        const std::optional<sv::TypeReference>& default_type)
    {
        const auto origin = model.add_origin(
            OriginKind::parsed, source, owner.origin, name);
        const auto id = model.add_declaration(owner.scope,
            DeclarationKind::type, name, source, origin);
        TypeReference semantic_type;
        semantic_type.source = source;
        semantic_type.spelling = name;
        const auto type_id = model.add_type(owner.scope,
            TypeKind::declaration, name, semantic_type, source, origin);
        sv::Declaration declaration;
        declaration.id = id;
        declaration.scope = owner.scope;
        declaration.form = sv::DeclarationForm::type_parameter;
        declaration.name = name;
        declaration.source = source;
        declaration.origin = origin;
        declaration.declared_type = type_id;
        declaration.default_type = default_type;
        hir.mutable_declarations().push_back(declaration);
        sv::TypeDefinition definition;
        definition.id = type_id;
        definition.declaration = id;
        definition.form = sv::TypeForm::type_parameter;
        definition.name = name;
        definition.source = source;
        definition.origin = origin;
        definition.base = default_type.value_or(sv::TypeReference { });
        hir.mutable_types().push_back(std::move(definition));
        owner.parameters.push_back({ id, name, true,
            std::nullopt, default_type, std::nullopt,
            source, origin, { } });
        owner.member_declarations.push_back(id);
        return id;
    }

    DeclarationId property(sv::ClassDeclaration& owner,
        const std::string& name, sv::TypeReference type,
        const bool static_storage = false)
    {
        const auto origin = model.add_origin(
            OriginKind::parsed, source, owner.origin, name);
        const auto id = model.add_declaration(owner.scope,
            DeclarationKind::variable, name, source, origin);
        sv::Declaration declaration;
        declaration.id = id;
        declaration.scope = owner.scope;
        declaration.form = sv::DeclarationForm::variable;
        declaration.name = name;
        declaration.source = source;
        declaration.origin = origin;
        declaration.type = type;
        hir.mutable_declarations().push_back(std::move(declaration));
        owner.properties.push_back({ id, name,
            sv::class_declaration_identity(owner) + "::" + name,
            sv::class_declaration_identity(owner), std::move(type),
            std::nullopt, sv::ClassVisibility::public_access,
            sv::ClassRandomKind::none, static_storage, false, false,
            source, origin, { } });
        owner.member_declarations.push_back(id);
        return id;
    }

    DeclarationId method(sv::ClassDeclaration& owner,
        const std::string& name, const std::string& profile,
        const bool virtual_method)
    {
        const auto origin = model.add_origin(
            OriginKind::parsed, source, owner.origin, name);
        const auto id = model.add_declaration(owner.scope,
            DeclarationKind::function, name, source, origin);
        sv::Declaration declaration;
        declaration.id = id;
        declaration.scope = owner.scope;
        declaration.form = sv::DeclarationForm::function;
        declaration.name = name;
        declaration.source = source;
        declaration.origin = origin;
        declaration.callable = sv::CallableProfile {
            true, integral_type(1), { }, sv::Lifetime::automatic
        };
        hir.mutable_declarations().push_back(std::move(declaration));
        owner.methods.push_back({ id, name,
            sv::class_declaration_identity(owner) + "::" + name,
            sv::class_declaration_identity(owner), profile,
            sv::ClassMethodKind::function,
            sv::ClassVisibility::public_access,
            sv::ClassLifetime::automatic,
            false, virtual_method, false, false, false, false, true,
            source, origin, { } });
        owner.member_declarations.push_back(id);
        return id;
    }

    void constraint(sv::ClassDeclaration& owner, const std::string& name)
    {
        const auto identity = sv::class_declaration_identity(owner)
            + "::" + name;
        const auto origin = model.add_origin(
            OriginKind::parsed, source, owner.origin, identity);
        owner.constraints.push_back({ name, identity,
            sv::class_declaration_identity(owner),
            sv::ClassVisibility::public_access, { }, false, false,
            false, true, source, origin });
        owner.composed_constraints.push_back(
            { name, identity, { }, false, true, true });
    }

    void covergroup(sv::ClassDeclaration& owner, const std::string& name)
    {
        sv::CovergroupDeclaration retained;
        retained.owner_kind = sv::CovergroupOwnerKind::class_declaration;
        retained.name = name;
        retained.owner_identity = sv::class_declaration_identity(owner);
        retained.canonical_identity = retained.owner_identity + "::" + name;
        retained.runtime_identity_prefix = retained.canonical_identity;
        retained.source = source;
        retained.origin = model.add_origin(OriginKind::parsed,
            source, owner.origin, retained.canonical_identity);
        owner.covergroups.push_back(std::move(retained));
    }

    static sv::TypeReference integral_type(const std::uint64_t width)
    {
        sv::TypeReference type;
        type.target.spelling = "logic";
        type.value_form = sv::TypeForm::packed_integral;
        type.executable_width = width;
        type.four_state = true;
        return type;
    }

    CompiledDesign finish()
    {
        return { std::move(model), std::move(hir), vhdl::Hir { } };
    }
};

struct Fixture {
    CompiledDesign design;
    DeclarationId derived_value_parameter;
    DeclarationId derived_type_parameter;
    DeclarationId derived_run;
};

Fixture make_fixture()
{
    FixtureBuilder builder;
    auto base = builder.make_class("Base");
    (void)builder.value_parameter(base, "WIDTH", "2");
    (void)builder.property(base, "base_value",
        FixtureBuilder::integral_type(8));
    (void)builder.method(base, "run", "run()", true);
    builder.hir.mutable_classes().push_back(base);

    auto interface = builder.make_class("Contract", true);
    (void)builder.type_parameter(interface, "T",
        FixtureBuilder::integral_type(8));
    (void)builder.method(interface, "apply", "apply(T)", true);
    builder.hir.mutable_classes().push_back(interface);

    auto derived = builder.make_class("Derived");
    const auto width = builder.value_parameter(derived, "WIDTH", "4");
    const auto type = builder.type_parameter(derived, "T",
        FixtureBuilder::integral_type(16));
    sv::ClassRelation base_relation;
    base_relation.name = "Base";
    base_relation.declaration_identity = "work::pkg::Base";
    base_relation.source = builder.source;
    base_relation.origin = derived.origin;
    base_relation.actuals.push_back({ std::string { "WIDTH" },
        sv::ActualKind::expression,
        builder.parameter_name(derived, width, "WIDTH"),
        std::nullopt, builder.source });
    derived.base = base_relation;
    derived.base_declaration_identity = base_relation.declaration_identity;
    sv::ClassRelation interface_relation;
    interface_relation.name = "Contract";
    interface_relation.declaration_identity = "work::pkg::Contract";
    interface_relation.source = builder.source;
    interface_relation.origin = derived.origin;
    sv::ActualAssociation type_actual;
    type_actual.formal = "T";
    type_actual.kind = sv::ActualKind::type;
    type_actual.type = FixtureBuilder::integral_type(16);
    type_actual.source = builder.source;
    interface_relation.actuals.push_back(type_actual);
    derived.implemented_interfaces.push_back(interface_relation);
    (void)builder.property(derived, "derived_value",
        FixtureBuilder::integral_type(16));
    (void)builder.property(derived, "counter",
        FixtureBuilder::integral_type(32), true);
    const auto run = builder.method(derived, "run", "run()", true);
    (void)builder.method(derived, "extra", "extra()", true);
    builder.constraint(derived, "legal");
    builder.covergroup(derived, "observed");
    builder.hir.mutable_classes().push_back(derived);

    return { builder.finish(), width, type, run };
}

const sv::ClassSpecialization& find_declaration(
    const sv::ClassSpecializationResult& result,
    const std::string_view identity,
    const std::string_view value = { })
{
    const auto found = std::ranges::find_if(
        result.specializations, [&](const auto& specialization) {
            return specialization.declaration_identity == identity
                && (value.empty()
                    || std::ranges::any_of(
                        specialization.parameters, [&](const auto& parameter) {
                            return parameter.display_identity == value;
                        }));
        });
    assert(found != result.specializations.end());
    return *found;
}

void test_defaults_relations_layout_and_members()
{
    const auto fixture = make_fixture();
    const auto result = sv::specialize_classes(fixture.design);
    assert(result.ok());
    const auto& derived = find_declaration(
        result, "work::pkg::Derived", "4");
    assert(derived.parameters.size() == 2);
    assert(derived.parameters[0].declaration
        == fixture.derived_value_parameter);
    assert(derived.parameters[0].canonical_identity
        == "value{integer:4}");
    assert(derived.parameters[1].declaration
        == fixture.derived_type_parameter);
    assert(derived.parameters[1].canonical_identity.starts_with("type{"));
    assert(derived.base);
    assert(derived.base->declaration_identity == "work::pkg::Base");
    assert(derived.interfaces.size() == 1);
    assert(derived.interfaces.front().declaration_identity
        == "work::pkg::Contract");
    assert(derived.properties.size() == 3);
    assert(derived.properties[0].name == "base_value");
    assert(derived.properties[0].bit_offset == 0);
    assert(derived.properties[1].name == "derived_value");
    assert(derived.properties[1].bit_offset == 8);
    assert(derived.instance_bit_width == 24);
    assert(derived.static_property_count == 1);
    assert(derived.methods.size() == 2);
    const auto run = std::ranges::find(
        derived.methods, fixture.derived_run,
        &sv::SpecializedClassMethod::declaration);
    assert(run != derived.methods.end() && run->virtual_slot == 0);
    const auto extra = std::ranges::find(
        derived.methods, std::string { "extra" },
        &sv::SpecializedClassMethod::name);
    assert(extra != derived.methods.end() && extra->virtual_slot == 1);
    assert(derived.constraints.size() == 1);
    assert(derived.constraints.front().selected_identity.ends_with("::legal"));
    assert(derived.covergroups.size() == 1);
    assert(derived.covergroups.front().canonical_identity.ends_with(
        "::observed"));
    assert((derived.source_dependencies
        == std::vector<std::string> { "classes.sv", "include.svh" }));
    assert(derived.source.valid() && derived.origin.valid());
}

void test_explicit_actuals_are_canonical_and_deterministic()
{
    const auto fixture = make_fixture();
    const auto& hir = fixture.design.systemverilog_hir;
    const auto width_expression = std::ranges::find_if(
        hir.expressions(), [](const auto& expression) {
            return expression.kind == sv::ExpressionKind::integer_literal
                && expression.text == "4";
        });
    assert(width_expression != hir.expressions().end());
    sv::ActualAssociation width;
    width.formal = "WIDTH";
    width.expression = width_expression->id;
    width.source = width_expression->source;
    sv::ActualAssociation type;
    type.formal = "T";
    type.kind = sv::ActualKind::type;
    type.type = FixtureBuilder::integral_type(32);
    type.source = width.source;
    std::vector<sv::ClassSpecializationRequest> requests {
        { "work::pkg::Derived", { width, type }, width.source }
    };
    const auto first = sv::specialize_classes(fixture.design, requests);
    std::ranges::reverse(requests.front().actuals);
    const auto second = sv::specialize_classes(fixture.design, requests);
    assert(first.ok() && second.ok());
    const auto first_identities = first.specializations
        | std::views::transform(
            &sv::ClassSpecialization::specialization_identity);
    const auto second_identities = second.specializations
        | std::views::transform(
            &sv::ClassSpecialization::specialization_identity);
    assert(std::ranges::equal(first_identities, second_identities));
}

void test_rejections()
{
    {
        auto fixture = make_fixture();
        const auto& expression = fixture.design.systemverilog_hir
            .expressions().front();
        sv::ActualAssociation actual;
        actual.formal = "WIDTH";
        actual.expression = expression.id;
        actual.source = expression.source;
        const std::vector<sv::ClassSpecializationRequest> requests {
            { "work::pkg::Derived", { actual, actual }, expression.source }
        };
        const auto result = sv::specialize_classes(fixture.design, requests);
        assert(!result.ok());
        assert(std::ranges::any_of(result.errors, [](const auto& error) {
            return error.kind
                == sv::ClassSpecializationErrorKind::duplicate_parameter;
        }));
    }
    {
        auto fixture = make_fixture();
        auto& base = fixture.design.mutable_systemverilog()
            .mutable_classes().front();
        const auto parameter = base.parameters.front().declaration;
        base.parameters.front().default_value.reset();
        const auto member = std::ranges::find(
            fixture.design.mutable_systemverilog().mutable_declarations(),
            parameter, &sv::Declaration::id);
        assert(member != fixture.design.mutable_systemverilog()
            .mutable_declarations().end());
        member->initializer.reset();
        const auto result = sv::specialize_classes(fixture.design);
        assert(!result.ok());
        assert(std::ranges::any_of(result.errors, [](const auto& error) {
            return error.kind
                == sv::ClassSpecializationErrorKind::missing_parameter;
        }));
    }
    {
        auto fixture = make_fixture();
        const auto& expression = fixture.design.systemverilog_hir
            .expressions().front();
        sv::ActualAssociation positional;
        positional.expression = expression.id;
        positional.source = expression.source;
        sv::ActualAssociation named;
        named.formal = "T";
        named.kind = sv::ActualKind::type;
        named.type = FixtureBuilder::integral_type(8);
        named.source = expression.source;
        const std::vector<sv::ClassSpecializationRequest> requests {
            { "work::pkg::Derived", { positional, named },
                expression.source }
        };
        const auto result = sv::specialize_classes(fixture.design, requests);
        assert(!result.ok());
        assert(std::ranges::any_of(result.errors, [](const auto& error) {
            return error.kind
                == sv::ClassSpecializationErrorKind::mixed_parameter_style;
        }));
    }
    {
        auto fixture = make_fixture();
        auto& classes = fixture.design.mutable_systemverilog().mutable_classes();
        classes[0].base = sv::ClassRelation {
            "Derived", "work::pkg::Derived", { },
            classes[0].source, classes[0].origin, { }
        };
        classes[0].base_declaration_identity = "work::pkg::Derived";
        const auto result = sv::specialize_classes(fixture.design);
        assert(!result.ok());
        assert(std::ranges::any_of(result.errors, [](const auto& error) {
            return error.kind
                == sv::ClassSpecializationErrorKind::inheritance_cycle;
        }));
    }
    {
        auto fixture = make_fixture();
        auto& derived = fixture.design.mutable_systemverilog()
            .mutable_classes().back();
        derived.implemented_interfaces.front().declaration_identity
            = "work::pkg::Base";
        derived.implemented_interfaces.front().name = "Base";
        derived.implemented_interfaces.front().actuals.clear();
        const auto result = sv::specialize_classes(fixture.design);
        assert(!result.ok());
        assert(std::ranges::any_of(result.errors, [](const auto& error) {
            return error.kind
                == sv::ClassSpecializationErrorKind::incompatible_relation;
        }));
    }
    {
        auto fixture = make_fixture();
        auto& declarations = fixture.design.mutable_systemverilog()
            .mutable_declarations();
        const auto method = std::ranges::find(
            declarations, fixture.derived_run,
            &sv::Declaration::id);
        assert(method != declarations.end());
        method->statements.push_back(StatementId::from_index(9999));
        const auto result = sv::specialize_classes(fixture.design);
        assert(!result.ok());
        assert(std::ranges::any_of(result.errors, [](const auto& error) {
            return error.kind
                == sv::ClassSpecializationErrorKind::unresolved_member;
        }));
    }
    {
        auto fixture = make_fixture();
        auto& derived = fixture.design.mutable_systemverilog()
            .mutable_classes().back();
        sv::TypeReference handle;
        handle.target.spelling = "Base#(8)";
        handle.value_form = sv::TypeForm::class_handle;
        handle.class_identity = "work::pkg::Base";
        auto& declaration = fixture.design.mutable_systemverilog()
            .mutable_declarations();
        const auto property = std::ranges::find(
            declaration, derived.properties.front().declaration,
            &sv::Declaration::id);
        assert(property != declaration.end());
        property->type = handle;
        derived.properties.front().type = handle;
        const auto result = sv::specialize_classes(fixture.design);
        assert(!result.ok());
        assert(std::ranges::any_of(result.errors, [](const auto& error) {
            return error.kind == sv::ClassSpecializationErrorKind::
                unsupported_parameterized_class_handle;
        }));
    }
}

} // namespace

int main()
{
    test_defaults_relations_layout_and_members();
    test_explicit_actuals_are_canonical_and_deterministic();
    test_rejections();
}
