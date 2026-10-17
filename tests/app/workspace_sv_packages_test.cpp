// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_internal.hpp"
#include "../../src/app/application_workspace_internal.hpp"

#include "fsim/app/design_artifact.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using namespace fsim;

void write_file(const std::filesystem::path& path, std::string_view contents)
{
    std::ofstream output { path };
    output << contents;
    assert(output.good());
}

project::Config config(const std::filesystem::path& source,
    std::string library = "work")
{
    project::Config result;
    result.base_directory = source.parent_path();
    result.project.name = "workspace-packages";
    result.project.time_resolution = "1ns";
    project::SourceSet sources;
    sources.language = project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = std::move(library);
    sources.files = { source };
    result.source_sets.push_back(std::move(sources));
    return result;
}

semantic::CompiledDesign compile_and_discard_source(
    const std::filesystem::path& source, std::string_view contents,
    std::string library = "work",
    const semantic::CompiledDesign* environment = nullptr)
{
    write_file(source, contents);
    diagnostic::Engine diagnostics;
    std::optional<std::string> bytes;
    std::vector<fsim::library::SourceNameMapping> consumer_mappings;
    {
        auto checked = app::application_detail::check_project_for_object(
            config(source, std::move(library)), diagnostics, environment);
        if (!checked)
            diagnostic::print_text(std::cerr, diagnostics);
        assert(checked && !diagnostics.has_error());
        const auto source_mappings
            = app::application_detail::compiled_cache_source_mappings(
                *checked, source.parent_path(), diagnostics);
        assert(source_mappings);
        assert(app::application_detail::relocate_compiled_design_sources(
            *checked, *source_mappings, diagnostics));
        for (const auto& mapping : *source_mappings)
            consumer_mappings.push_back({ mapping.logical_name, mapping.producer_name });
        bytes = app::serialize_compiled_hir_bundle(*checked, diagnostics);
        if (!bytes)
            diagnostic::print_text(std::cerr, diagnostics);
        assert(bytes);
    }
    // The compiler workspace, its syntax, and the physical package source are
    // gone before the next source is analyzed against the decoded provider.
    std::filesystem::remove(source);
    auto decoded = app::deserialize_compiled_hir_bundle(
        *bytes, "workspace-provider", diagnostics);
    if (!decoded)
        diagnostic::print_text(std::cerr, diagnostics);
    assert(decoded && !diagnostics.has_error());
    assert(app::application_detail::relocate_compiled_design_sources(
        *decoded, consumer_mappings, diagnostics));
    return std::move(*decoded);
}

constexpr std::string_view provider_source = R"(
package values;
  parameter int N = 7;
  typedef logic [N-1:0] word_t;
  function automatic int increment(input int value);
    return value + N;
  endfunction
  task ping();
  endtask
  class Box;
    int value = 7;
    task set(input int next_value);
      value = next_value;
    endtask
    function int get();
      return value;
    endfunction
    static function int twice(input int value);
      return value * 2;
    endfunction
    static task tick();
    endtask
  endclass
endpackage
)";

void expect_signal(const semantic::CompiledDesign& compiled,
    std::string_view expected)
{
    app::CheckedProject checked;
    static_cast<semantic::CompiledDesign&>(checked) = compiled;
    app::application_detail::install_compiled_class_specializations(checked);
    const elaboration::Root root { "sv:work.consumer", "dut" };
    auto elaborated = elaboration::elaborate(checked,
        std::span<const elaboration::Root> { &root, 1U }, { }, { }, nullptr, { });
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics)
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
    assert(elaborated.ok() && elaborated.design);
    app::BuiltProject built;
    const auto runtime_paths = app::application_detail::make_runtime_path_views(
        *elaborated.design);
    built.design_ir = app::application_detail::build_design_ir(
        checked, *elaborated.design, runtime_paths);
    assert(built.design_ir.valid(checked.semantics));
    assert(elaborated.design->rebind_path_table(built.design_ir.hierarchy_paths()));
    for (const auto& unit : checked.systemverilog_hir.units()) {
        const auto& owner = checked.semantics.units().at(unit.id.value());
        const auto language = owner.language == semantic::Language::verilog
            ? project::Language::verilog : project::Language::system_verilog;
        const auto identity = unit.library + "::" + unit.name;
        built.verilog_unit_revisions.emplace(identity,
            app::application_detail::frontend_standard_revision(language, unit.standard));
        built.verilog_unit_compatibility_profiles.emplace(
            identity, unit.compatibility_profile);
    }
    built.design = std::move(*elaborated.design);
    built.semantics = std::move(checked.semantics);
    built.systemverilog_hir = std::move(checked.systemverilog_hir);
    built.vhdl_hir = std::move(checked.vhdl_hir);
    built.compiled_systemverilog_class_specializations
        = std::move(checked.compiled_systemverilog_class_specializations);
    built.time_resolution = "1ns";
    app::Simulation simulation { std::move(built), 1000,
        app::SimulationEngine::interpreter };
    const auto signal = simulation.find_signal("dut.result");
    assert(signal);
    const auto result = simulation.run();
    assert(result.status == runtime::RunStatus::completed);
    assert(simulation.read_signal(*signal).to_msb_string() == expected);
}

app::application_detail::WorkspaceCatalogs provider_catalogs(
    const semantic::CompiledDesign& provider, const std::string& library)
{
    app::workspace::LibraryCatalog catalog;
    catalog.location.name = library;
    const auto append = [&](fsim::library::UnitIndexEntry unit,
                            const std::string& fingerprint) {
        app::workspace::ArtifactRecord artifact;
        artifact.id = fingerprint;
        artifact.fingerprint = fingerprint;
        artifact.units.push_back({ std::move(unit), "provider-source" });
        catalog.artifacts.push_back(std::move(artifact));
    };
    for (const auto& [id, unit] : app::application_detail::compiled_object_unit_entries(
             provider, library)) {
        (void)id;
        append(unit, "sv-package-fingerprint");
        if (unit.kind == "package") {
            auto unrelated = unit;
            unrelated.language = "vhdl";
            unrelated.standard = "2008";
            append(std::move(unrelated), "unrelated-vhdl-fingerprint");
        }
    }
    for (const auto& declaration : provider.systemverilog_hir.classes()) {
        const auto unit = app::application_detail::compiled_class_metadata_entry(
            provider, declaration, library);
        if (unit)
            append(*unit, "sv-class-fingerprint");
    }
    return { std::move(catalog) };
}

void expect_catalog_dependencies(const semantic::CompiledDesign& consumer,
    const semantic::CompiledDesign& provider, const std::string& library,
    const bool expect_class)
{
    const auto owner = consumer.find_unit(semantic::UnitKind::verilog_module,
        "work", "consumer");
    assert(owner);
    const std::array owners { owner->identity->id };
    const auto dependencies = app::application_detail::workspace_dependencies(
        consumer, owners, provider_catalogs(provider, library));
    assert(std::ranges::any_of(dependencies, [&](const auto& dependency) {
        return dependency.library == library
            && dependency.unit.language == "systemverilog"
            && dependency.unit.kind == "package" && dependency.unit.name == "values"
            && dependency.fingerprint == "sv-package-fingerprint";
    }));
    assert(std::ranges::none_of(dependencies, [](const auto& dependency) {
        return dependency.unit.language == "vhdl"
            || dependency.fingerprint == "unrelated-vhdl-fingerprint";
    }));
    if (!expect_class)
        return;
    const auto declaration = std::ranges::find(provider.systemverilog_hir.classes(),
        "Box", &semantic::sv::ClassDeclaration::name);
    assert(declaration != provider.systemverilog_hir.classes().end());
    const auto metadata = app::application_detail::compiled_class_metadata_entry(
        provider, *declaration, library);
    assert(metadata);
    assert(std::ranges::any_of(dependencies, [&](const auto& dependency) {
        return dependency.library == library && dependency.unit.kind == "class"
            && dependency.unit.name == metadata->name
            && dependency.fingerprint == "sv-class-fingerprint";
    }));
}

void independent_package(const std::filesystem::path& directory,
    const std::string& library)
{
    const auto provider = compile_and_discard_source(
        directory / "values.sv", provider_source, library);
    const auto consumer = compile_and_discard_source(directory / "consumer.sv", R"(
module consumer;
  import values::*;
  word_t result;
  Box item;
  initial begin
    item = new();
    item.set(N);
    result = Box::twice(N) + item.get();
  end
endmodule
)", "work", &provider);
    expect_signal(consumer, "0010101");
    expect_catalog_dependencies(consumer, provider, library, true);
}

void transitive_package(const std::filesystem::path& directory)
{
    const auto provider = compile_and_discard_source(
        directory / "values.sv", provider_source, "external");
    const auto facade = compile_and_discard_source(directory / "facade.sv", R"(
package facade;
  import values::*;
  export values::*;
endpackage
)", "work", &provider);
    const auto consumer = compile_and_discard_source(directory / "consumer.sv", R"(
module consumer;
  import facade::*;
  word_t result;
  Box item;
  initial begin
    item = new();
    result = increment(item.value);
  end
endmodule
)", "work", &facade);
    expect_signal(consumer, "0001110");
}

void qualified_package(const std::filesystem::path& directory)
{
    const auto provider = compile_and_discard_source(
        directory / "values.sv", provider_source, "external");
    const auto consumer = compile_and_discard_source(directory / "consumer.sv", R"(
module consumer;
  values::word_t result;
  initial result = values::increment(values::N);
endmodule
)", "work", &provider);
    expect_signal(consumer, "0001110");
    expect_catalog_dependencies(consumer, provider, "external", false);
    const auto unit = consumer.find_unit(semantic::UnitKind::verilog_module,
        "work", "consumer");
    assert(unit);
    assert(std::ranges::any_of(consumer.references(), [&](const auto& reference) {
        return reference.owner == unit->identity->id
            && reference.kind == semantic::CompiledReferenceKind::package
            && reference.library == "external" && reference.name == "values";
    }));

    const auto package_member_call = [](const semantic::CompiledDesign& design) {
        return std::ranges::find_if(
            design.systemverilog_hir.expressions(),
            [](const semantic::sv::Expression& expression) {
                return expression.kind == semantic::sv::ExpressionKind::call
                    && expression.referenced_name
                    && expression.referenced_name->spelling
                        == "external::values::increment";
            });
    };
    const auto resolved_package_member_identity = [&](
        const semantic::CompiledDesign& design) {
        const auto call = package_member_call(design);
        assert(call != design.systemverilog_hir.expressions().end());
        assert(call->referenced_name
            && call->referenced_name->selected);
        const auto selected_id = *call->referenced_name->selected;
        const auto declaration = std::ranges::find(
            design.systemverilog_hir.declarations(), selected_id,
            &semantic::sv::Declaration::id);
        assert(declaration != design.systemverilog_hir.declarations().end());
        assert(declaration->form
            == semantic::sv::DeclarationForm::function);
        const auto& semantic_declaration
            = design.semantics.declarations()[selected_id.value()];
        assert(semantic_declaration.scope.valid());
        const auto package_id = design.semantics.scopes()[
            semantic_declaration.scope.value()].unit;
        assert(package_id.valid());
        const auto& package = design.semantics.units()[package_id.value()];
        const auto& span = design.semantics.source_spans()[
            declaration->source.value()];
        const auto& source_file = design.semantics.source_files()[
            span.file.value()];
        return semantic::CompiledSystemVerilogPackageDeclarationIdentity {
            package.library,
            package.name,
            declaration->form,
            declaration->name,
            source_file.content_digest,
            span.begin.offset,
            span.end.offset
        };
    };
    const auto package_member_use = [&](
        const semantic::CompiledDesign& design) {
        const auto call = package_member_call(design);
        assert(call != design.systemverilog_hir.expressions().end());
        const auto reference = std::ranges::find_if(
            design.references(), [&](const semantic::CompiledReference& value) {
                return value.systemverilog_package_member
                    && value.systemverilog_package_member->expression
                        == std::optional { call->id };
            });
        assert(reference != design.references().end());
        const auto& use = *reference->systemverilog_package_member;
        assert(use.selected_present && use.selected_complete);
        assert(use.package_selected);
        assert(use.package_selected->library == "external");
        assert(use.package_selected->package == "values");
        assert(use.package_selected->form
            == semantic::sv::DeclarationForm::function);
        assert(use.package_selected->name == "increment");
        assert(!use.package_selected->source_digest.empty());
        assert(use.package_selected->source_begin
            < use.package_selected->source_end);
        return *use.package_selected;
    };
    const auto expected_identity = resolved_package_member_identity(consumer);

    const std::array consumer_units { unit->identity->id };
    const std::vector<std::string> no_supporting_libraries;
    auto projected = semantic::extract_compiled_units(
        consumer, consumer_units, no_supporting_libraries);
    assert(projected.ok() && projected.design);
    assert(std::ranges::none_of(projected.design->systemverilog_hir.units(),
        [](const semantic::sv::Unit& value) {
            return value.kind == semantic::sv::UnitKind::package;
        }));
    assert(package_member_use(*projected.design) == expected_identity);
    const auto projected_member_use = std::ranges::find_if(
        projected.design->mutable_references(),
        [](const semantic::CompiledReference& reference) {
            return reference.systemverilog_package_member
                && reference.systemverilog_package_member->expression;
        });
    assert(projected_member_use
        != projected.design->mutable_references().end());
    auto& overload_use
        = *projected_member_use->systemverilog_package_member;
    overload_use.overloads.clear();
    // Keep one valid candidate so this round-trip directly covers the
    // nonempty overload-vector wire representation.
    overload_use.overload_count = 1U;
    overload_use.overloads.push_back({ 0U, std::nullopt, expected_identity });
    assert(projected.design->valid());
    diagnostic::Engine roundtrip_diagnostics;
    app::CheckedProject portable_projected;
    static_cast<semantic::CompiledDesign&>(portable_projected)
        = std::move(*projected.design);
    const auto projected_source_mappings
        = app::application_detail::compiled_cache_source_mappings(
            portable_projected, directory, roundtrip_diagnostics);
    assert(projected_source_mappings && !roundtrip_diagnostics.has_error());
    assert(app::application_detail::relocate_compiled_design_sources(
        portable_projected, *projected_source_mappings,
        roundtrip_diagnostics));
    assert(!roundtrip_diagnostics.has_error());
    assert(package_member_use(portable_projected) == expected_identity);
    const auto projected_bytes = app::serialize_compiled_hir_bundle(
        portable_projected, roundtrip_diagnostics);
    assert(projected_bytes && !roundtrip_diagnostics.has_error());
    auto stale_bundle_schema = *projected_bytes;
    assert(stale_bundle_schema.size() >= 12U);
    stale_bundle_schema[8] = '\x01';
    stale_bundle_schema[9] = '\x00';
    stale_bundle_schema[10] = '\x00';
    stale_bundle_schema[11] = '\x00';
    diagnostic::Engine stale_bundle_diagnostics;
    assert(!app::deserialize_compiled_hir_bundle(stale_bundle_schema,
        "stale-compiled-hir-bundle", stale_bundle_diagnostics));
    assert(stale_bundle_diagnostics.has_error());
    auto roundtrip = app::deserialize_compiled_hir_bundle(
        *projected_bytes, "projected-package-member-use", roundtrip_diagnostics);
    assert(roundtrip && !roundtrip_diagnostics.has_error());
    assert(std::ranges::none_of(roundtrip->systemverilog_hir.units(),
        [](const semantic::sv::Unit& value) {
            return value.kind == semantic::sv::UnitKind::package;
        }));
    assert(package_member_use(*roundtrip) == expected_identity);
    const auto roundtrip_member_use = std::ranges::find_if(
        roundtrip->references(),
        [](const semantic::CompiledReference& reference) {
            return reference.systemverilog_package_member
                && reference.systemverilog_package_member->expression;
        });
    assert(roundtrip_member_use != roundtrip->references().end());
    assert(roundtrip_member_use->systemverilog_package_member->overload_count
        == 1U);
    assert(roundtrip_member_use->systemverilog_package_member->overloads.size()
        == 1U);
    assert(roundtrip_member_use->systemverilog_package_member->overloads[0]
            .position
        == 0U);
    assert(roundtrip_member_use->systemverilog_package_member->overloads[0]
            .package_declaration
        == std::optional { expected_identity });

    std::vector<semantic::CompiledDesign> link_inputs;
    link_inputs.push_back(std::move(*roundtrip));
    link_inputs.push_back(provider);
    auto relinked = semantic::link_compiled_designs(std::move(link_inputs));
    assert(relinked.ok() && relinked.design);
    const auto restored_call = std::ranges::find_if(
        relinked.design->systemverilog_hir.expressions(),
        [](const semantic::sv::Expression& expression) {
            return expression.kind == semantic::sv::ExpressionKind::call
                && expression.referenced_name
                && expression.referenced_name->spelling
                    == "external::values::increment";
        });
    assert(restored_call
        != relinked.design->systemverilog_hir.expressions().end());
    assert(restored_call->referenced_name
        && restored_call->referenced_name->selected);
    const auto selected_id = *restored_call->referenced_name->selected;
    assert(restored_call->referenced_name->overloads.size() == 1U);
    assert(restored_call->referenced_name->overloads.front() == selected_id);
    const auto selected_decl = std::ranges::find(
        relinked.design->systemverilog_hir.declarations(), selected_id,
        &semantic::sv::Declaration::id);
    assert(selected_decl
        != relinked.design->systemverilog_hir.declarations().end());
    const auto& semantic_decl
        = relinked.design->semantics.declarations()[selected_id.value()];
    const auto package_id
        = relinked.design->semantics.scopes()[semantic_decl.scope.value()].unit;
    const auto& package
        = relinked.design->semantics.units()[package_id.value()];
    const auto& selected_span
        = relinked.design->semantics.source_spans()[
            selected_decl->source.value()];
    const auto& selected_file
        = relinked.design->semantics.source_files()[
            selected_span.file.value()];
    assert(package.library == expected_identity.library);
    assert(package.name == expected_identity.package);
    assert(selected_decl->name == expected_identity.name);
    assert(selected_file.content_digest == expected_identity.source_digest);
    assert(selected_span.begin.offset == expected_identity.source_begin);
}

void inherited_package_class(const std::filesystem::path& directory)
{
    const auto provider = compile_and_discard_source(
        directory / "values.sv", provider_source, "external");
    const auto consumer = compile_and_discard_source(directory / "consumer.sv", R"(
module consumer;
  import values::*;
  class Derived extends Box;
    function int incremented();
      return get() + 1;
    endfunction
  endclass
  word_t result;
  Derived item;
  initial begin
    item = new();
    result = item.incremented();
  end
endmodule
)", "work", &provider);
    expect_signal(consumer, "0001000");
}

void type_only_global_class(const std::filesystem::path& directory)
{
    const auto provider = compile_and_discard_source(directory / "global.sv", R"(
class Global;
  int value;
endclass
package global_types;
  typedef Global handle_t;
endpackage
)", "external");
    const auto consumer = compile_and_discard_source(directory / "consumer.sv", R"(
module consumer;
  global_types::handle_t unused_handle;
endmodule
)", "work", &provider);
    const auto owner = consumer.find_unit(semantic::UnitKind::verilog_module,
        "work", "consumer");
    assert(owner);
    const auto declaration = std::ranges::find(provider.systemverilog_hir.classes(),
        "Global", &semantic::sv::ClassDeclaration::name);
    assert(declaration != provider.systemverilog_hir.classes().end());
    assert(declaration->canonical_identity.find("::$unit@") != std::string::npos);
    assert(std::ranges::any_of(consumer.references(), [&](const auto& reference) {
        return reference.owner == owner->identity->id
            && reference.kind == semantic::CompiledReferenceKind::class_declaration
            && reference.library == "external"
            && reference.secondary_name == declaration->canonical_identity
            && reference.target;
    }));
    const std::array owners { owner->identity->id };
    const auto dependencies = app::application_detail::workspace_dependencies(
        consumer, owners, provider_catalogs(provider, "external"));
    assert(std::ranges::any_of(dependencies, [&](const auto& dependency) {
        return dependency.library == "external" && dependency.unit.kind == "class"
            && dependency.unit.name == semantic::sv::class_declaration_identity(*declaration)
            && dependency.fingerprint == "sv-class-fingerprint";
    }));
}

void qualified_tasks(const std::filesystem::path& directory)
{
    const auto provider = compile_and_discard_source(
        directory / "values.sv", provider_source, "external");
    const auto consumer = compile_and_discard_source(directory / "tasks.sv", R"(
module package_task_consumer;
  initial values::ping();
endmodule
module static_task_consumer;
  initial values::Box::tick();
endmodule
)", "work", &provider);
    for (const auto name : { "package_task_consumer", "static_task_consumer" }) {
        const auto owner = consumer.find_unit(semantic::UnitKind::verilog_module,
            "work", name);
        assert(owner);
        assert(std::ranges::any_of(consumer.references(), [&](const auto& reference) {
            return reference.owner == owner->identity->id
                && reference.kind == semantic::CompiledReferenceKind::package
                && reference.library == "external" && reference.name == "values"
                && reference.target;
        }));
        if (std::string_view { name } == "static_task_consumer") {
            assert(std::ranges::any_of(consumer.references(), [&](const auto& reference) {
                return reference.owner == owner->identity->id
                    && reference.kind == semantic::CompiledReferenceKind::class_declaration
                    && reference.library == "external"
                    && reference.secondary_name == "external::values::Box"
                    && reference.target;
            }));
        }
    }

    const auto owner = consumer.find_unit(semantic::UnitKind::verilog_module,
        "work", "package_task_consumer");
    assert(owner);
    const std::array owner_selection { owner->identity->id };
    const std::vector<std::string> no_supporting_libraries;
    auto projected = semantic::extract_compiled_units(
        consumer, owner_selection, no_supporting_libraries);
    assert(projected.ok() && projected.design);
    assert(std::ranges::none_of(projected.design->systemverilog_hir.units(),
        [](const semantic::sv::Unit& unit) {
            return unit.kind == semantic::sv::UnitKind::package;
        }));
    // The package sidecar keeps the resolved identity through projection.
    const auto task_statement = [](const semantic::CompiledDesign& design) {
        const auto package_task_use = std::ranges::find_if(
            design.references(),
            [](const semantic::CompiledReference& reference) {
                if (!reference.systemverilog_package_member)
                    return false;
                const auto& use = *reference.systemverilog_package_member;
                return use.statement && use.package_selected
                    && use.package_selected->library == "external"
                    && use.package_selected->package == "values"
                    && use.package_selected->form
                        == semantic::sv::DeclarationForm::task
                    && use.package_selected->name == "ping";
            });
        if (package_task_use == design.references().end())
            return design.systemverilog_hir.statements().end();
        const auto statement_id
            = package_task_use->systemverilog_package_member->statement;
        return std::ranges::find_if(
            design.systemverilog_hir.statements(),
            [&](const semantic::sv::Statement& statement) {
                return statement.kind
                        == semantic::sv::StatementKind::task_call
                    && statement.id == *statement_id;
            });
    };
    const auto projected_task = task_statement(*projected.design);
    assert(projected_task
        != projected.design->systemverilog_hir.statements().end());
    const auto package_task_use = [&](const semantic::CompiledDesign& design) {
        const auto call = task_statement(design);
        assert(call != design.systemverilog_hir.statements().end());
        const auto reference = std::ranges::find_if(
            design.references(), [&](const semantic::CompiledReference& value) {
                return value.systemverilog_package_member
                    && value.systemverilog_package_member->statement
                    == std::optional { call->id };
            });
        assert(reference != design.references().end());
        const auto& use = *reference->systemverilog_package_member;
        assert(use.selected_present && use.selected_complete);
        assert(use.package_selected);
        assert(use.package_selected->library == "external");
        assert(use.package_selected->package == "values");
        assert(use.package_selected->form == semantic::sv::DeclarationForm::task);
        assert(use.package_selected->name == "ping");
        return *use.package_selected;
    };
    const auto expected_task_identity = package_task_use(*projected.design);
    diagnostic::Engine roundtrip_diagnostics;
    app::CheckedProject portable_projected;
    static_cast<semantic::CompiledDesign&>(portable_projected)
        = std::move(*projected.design);
    const auto projected_source_mappings
        = app::application_detail::compiled_cache_source_mappings(
            portable_projected, directory, roundtrip_diagnostics);
    assert(projected_source_mappings && !roundtrip_diagnostics.has_error());
    assert(app::application_detail::relocate_compiled_design_sources(
        portable_projected, *projected_source_mappings,
        roundtrip_diagnostics));
    assert(!roundtrip_diagnostics.has_error());
    assert(package_task_use(portable_projected) == expected_task_identity);
    const auto bytes = app::serialize_compiled_hir_bundle(
        portable_projected, roundtrip_diagnostics);
    assert(bytes && !roundtrip_diagnostics.has_error());
    auto roundtrip = app::deserialize_compiled_hir_bundle(
        *bytes, "projected-package-task-use", roundtrip_diagnostics);
    assert(roundtrip && !roundtrip_diagnostics.has_error());
    assert(package_task_use(*roundtrip) == expected_task_identity);

    std::vector<semantic::CompiledDesign> link_inputs;
    link_inputs.push_back(std::move(*roundtrip));
    link_inputs.push_back(provider);
    auto relinked = semantic::link_compiled_designs(std::move(link_inputs));
    assert(relinked.ok() && relinked.design);
    const auto restored_task = task_statement(*relinked.design);
    assert(restored_task
        != relinked.design->systemverilog_hir.statements().end());
    assert(restored_task->task.selected);
    const auto selected_id = *restored_task->task.selected;
    assert(selected_id.valid());
    assert(selected_id.value()
        < relinked.design->semantics.declarations().size());
    const auto declaration = std::ranges::find(
        relinked.design->systemverilog_hir.declarations(),
        selected_id, &semantic::sv::Declaration::id);
    assert(declaration != relinked.design->systemverilog_hir.declarations().end());
    assert(declaration->form == semantic::sv::DeclarationForm::task);
    assert(declaration->name == "ping");
    const auto& semantic_declaration
        = relinked.design->semantics.declarations()[selected_id.value()];
    assert(semantic_declaration.scope.valid());
    assert(semantic_declaration.scope.value()
        < relinked.design->semantics.scopes().size());
    const auto package_id = relinked.design->semantics.scopes()[
        semantic_declaration.scope.value()].unit;
    assert(package_id.valid());
    assert(package_id.value() < relinked.design->semantics.units().size());
    const auto& package
        = relinked.design->semantics.units()[package_id.value()];
    assert(declaration->source.valid());
    assert(declaration->source.value()
        < relinked.design->semantics.source_spans().size());
    const auto& selected_span = relinked.design->semantics.source_spans()[
        declaration->source.value()];
    assert(selected_span.file.valid());
    assert(selected_span.file.value()
        < relinked.design->semantics.source_files().size());
    const auto& selected_file = relinked.design->semantics.source_files()[
        selected_span.file.value()];
    const semantic::CompiledSystemVerilogPackageDeclarationIdentity
        restored_task_identity {
            package.library,
            package.name,
            declaration->form,
            declaration->name,
            selected_file.content_digest,
            selected_span.begin.offset,
            selected_span.end.offset
        };
    assert(restored_task_identity == expected_task_identity);
}

void ambiguous_package(const std::filesystem::path& directory)
{
    auto left = compile_and_discard_source(directory / "left.sv", provider_source, "left");
    auto right = compile_and_discard_source(directory / "right.sv", provider_source, "right");
    auto linked = semantic::link_compiled_designs({ std::move(left), std::move(right) });
    assert(linked.ok());
    const auto source = directory / "ambiguous.sv";
    write_file(source, "module consumer; import values::*; word_t result; endmodule\n");
    diagnostic::Engine diagnostics;
    assert(!app::application_detail::check_project_for_object(
        config(source), diagnostics, &*linked.design));
    assert(std::ranges::any_of(diagnostics.diagnostics(), [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-SV-PACKAGE-001";
    }));
}

void qualified_profile_mismatch(const std::filesystem::path& directory)
{
    const auto provider = compile_and_discard_source(
        directory / "values.sv", provider_source);
    const auto source = directory / "profile.sv";
    write_file(source, "module consumer; logic [6:0] result; "
        "initial result = values::N; endmodule\n");
    auto consumer_config = config(source);
    consumer_config.source_sets.front().standard = "2012";
    diagnostic::Engine diagnostics;
    assert(!app::application_detail::check_project_for_object(
        consumer_config, diagnostics, &provider));
    assert(std::ranges::any_of(diagnostics.diagnostics(), [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-FE-STANDARD-003";
    }));
}

} // namespace

int main()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path()
        / ("fsim-workspace-sv-packages-" + std::to_string(stamp));
    std::filesystem::create_directories(directory);
    independent_package(directory, "work");
    independent_package(directory, "external");
    transitive_package(directory);
    qualified_package(directory);
    inherited_package_class(directory);
    type_only_global_class(directory);
    qualified_tasks(directory);
    ambiguous_package(directory);
    qualified_profile_mismatch(directory);
    std::filesystem::remove_all(directory);
}
