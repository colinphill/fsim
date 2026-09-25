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
