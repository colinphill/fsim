// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/trace_archive.hpp"
#include "fsim/artifact/object.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"

#include "../../src/app/application_workspace_objects.hpp"
#include "../../src/app/application_workspace.hpp"
#include "../../src/app/application_workspace_internal.hpp"
#include "../../src/app/application_workspace_selection.hpp"
#include "../../src/app/application_workspace_store.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {

namespace detail = fsim::app::application_detail;
namespace workspace = fsim::app::workspace;

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator iterator {
                 path, error }, end;
             !error && iterator != end; iterator.increment(error)) {
            std::filesystem::permissions(iterator->path(),
                std::filesystem::perms::owner_all,
                std::filesystem::perm_options::add, error);
            error.clear();
        }
        std::filesystem::remove_all(path, error);
    }
};

void write_file(const std::filesystem::path& path, const std::string_view text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output { path, std::ios::binary };
    assert(output);
    output << text;
    assert(output);
}

void copy_read_only_object_tree(const std::filesystem::path& source,
    const std::filesystem::path& destination)
{
    assert(std::filesystem::is_directory(source));
    assert(!std::filesystem::exists(destination));
    std::filesystem::create_directory(destination);
    for (const auto& entry : std::filesystem::recursive_directory_iterator { source }) {
        const auto relative = entry.path().lexically_relative(source);
        const auto target = destination / relative;
        const auto status = entry.symlink_status();
        if (std::filesystem::is_directory(status)) {
            std::filesystem::create_directory(target);
        } else {
            assert(std::filesystem::is_regular_file(status));
            std::filesystem::copy_file(entry.path(), target);
        }
    }
}

void write_object_metadata(const std::filesystem::path& object,
    const fsim::artifact::ObjectMetadata& metadata)
{
    const auto path = object / fsim::artifact::kObjectMetadataFilename;
    std::filesystem::permissions(path,
        std::filesystem::perms::owner_all,
        std::filesystem::perm_options::add);
    write_file(path, fsim::artifact::serialize_object_metadata(metadata));
    std::filesystem::permissions(path,
        std::filesystem::perms::owner_read,
        std::filesystem::perm_options::replace);
}

detail::WorkspaceObjectSelection compile(const std::filesystem::path& root,
    const std::string& name, const std::string_view text)
{
    const auto source = root / (name + ".sv");
    write_file(source, text);
    fsim::project::Config config;
    config.base_directory = root;
    config.project.name = name;
    config.build.cache_path = root / "cache";
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files = { source };
    config.source_sets.push_back(std::move(sources));
    detail::WorkspaceObjectSelection result;
    result.path = root / (name + ".fsimobj");
    fsim::diagnostic::Engine diagnostics;
    const auto success = fsim::app::compile_artifact(
        config, result.path, diagnostics);
    if (!success) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(success && !diagnostics.has_error());
    auto metadata = fsim::artifact::load_object_metadata(result.path, diagnostics);
    assert(metadata && !diagnostics.has_error());
    result.active_units = std::move(metadata->units);
    // Managed loading must rely on the artifact, including for classes and
    // primitives, after the original source has gone away.
    std::filesystem::remove(source);
    return result;
}

detail::WorkspaceObjectSelection compile_vhdl(
    const std::filesystem::path& root, const std::string& name,
    const std::string_view text)
{
    const auto source = root / (name + ".vhd");
    write_file(source, text);
    fsim::project::Config config;
    config.base_directory = root;
    config.project.name = name;
    config.build.cache_path = root / "cache";
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.files = { source };
    config.source_sets.push_back(std::move(sources));
    detail::WorkspaceObjectSelection result;
    result.path = root / (name + ".fsimobj");
    fsim::diagnostic::Engine diagnostics;
    const auto success = fsim::app::compile_artifact(
        config, result.path, diagnostics);
    if (!success) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(success && !diagnostics.has_error());
    auto metadata = fsim::artifact::load_object_metadata(result.path, diagnostics);
    assert(metadata && !diagnostics.has_error());
    result.active_units = std::move(metadata->units);
    std::filesystem::remove(source);
    return result;
}

std::optional<detail::CompilationWorkspace> check_vhdl_with_imported(
    const std::filesystem::path& root, const std::string& name,
    const std::string_view text, const fsim::semantic::CompiledDesign& imported,
    fsim::diagnostic::Engine& diagnostics)
{
    const auto source = root / (name + ".vhd");
    write_file(source, text);
    fsim::project::Config config;
    config.base_directory = root;
    config.project.name = name;
    config.build.jobs = 8;
    config.build.cache_path = root / "cache";
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.files = { source };
    config.source_sets.push_back(std::move(sources));
    auto checked = detail::check_project_for_object(config, diagnostics, &imported);
    std::filesystem::remove(source);
    return checked;
}

struct LegacyCatalogFixture {
    std::unique_ptr<workspace::LibraryTransaction> transaction;
    workspace::LibraryCatalog catalog;
};

LegacyCatalogFixture make_legacy_catalog(
    workspace::Store& store,
    const std::vector<detail::WorkspaceObjectSelection>& objects)
{
    std::filesystem::create_directories(store.root());
    std::string error;
    auto transaction = store.begin_library("work", error);
    assert(transaction && error.empty());
    LegacyCatalogFixture result;
    result.catalog = transaction->catalog();
    for (std::size_t index = 0; index < objects.size(); ++index) {
        fsim::diagnostic::Engine diagnostics;
        const auto metadata = fsim::artifact::load_object_metadata(
            objects[index].path, diagnostics);
        assert(metadata && !diagnostics.has_error());
        auto record = transaction->allocate_artifact(
            workspace::ArtifactKind::Hdl, error);
        assert(record && error.empty());
        copy_read_only_object_tree(objects[index].path,
            transaction->artifact_path(*record));
        auto legacy_metadata = fsim::artifact::load_object_metadata(
            transaction->artifact_path(*record), diagnostics);
        assert(legacy_metadata && !diagnostics.has_error());
        for (auto& unit : legacy_metadata->units) {
            if (unit.language == "vhdl") {
                unit.name_form = fsim::library::VhdlIdentifierForm::unknown;
                unit.primary_name_form
                    = fsim::library::VhdlIdentifierForm::unknown;
            }
        }
        legacy_metadata->compilation_digest
            = fsim::artifact::compute_object_compilation_digest(
                *legacy_metadata);
        const auto metadata_path = transaction->artifact_path(*record)
            / fsim::artifact::kObjectMetadataFilename;
        std::filesystem::permissions(metadata_path,
            std::filesystem::perms::owner_all,
            std::filesystem::perm_options::add);
        write_file(metadata_path,
            fsim::artifact::serialize_object_metadata(*legacy_metadata));
        std::filesystem::permissions(metadata_path,
            std::filesystem::perms::owner_read,
            std::filesystem::perm_options::replace);
        const auto source_key = "legacy-" + std::to_string(index) + ".vhd";
        record->sources = { source_key };
        record->fingerprint = legacy_metadata->compilation_digest;
        for (auto unit : legacy_metadata->units) {
            if (unit.language == "vhdl") {
                unit.name_form = fsim::library::VhdlIdentifierForm::unknown;
                unit.primary_name_form
                    = fsim::library::VhdlIdentifierForm::unknown;
            }
            record->units.push_back({ std::move(unit), source_key });
        }
        result.catalog.artifacts.push_back(std::move(*record));
    }
    result.transaction = std::move(transaction);
    return result;
}

bool has_diagnostic_code(const fsim::diagnostic::Engine& diagnostics,
    const std::string_view code)
{
    return std::ranges::any_of(diagnostics.diagnostics(),
        [&](const auto& item) { return item.code == code; });
}

detail::WorkspaceObjectSelection select(
    detail::WorkspaceObjectSelection object, const std::string_view name)
{
    std::erase_if(object.active_units, [&](const auto& unit) {
        return unit.name != name && !unit.name.ends_with("::" + std::string { name });
    });
    assert(object.active_units.size() == 1U);
    return object;
}

void check_catalog_selection(const std::filesystem::path& root)
{
    const auto object = compile(root, "selection", R"(
module retained;
  logic q;
  initial q = 1'b1;
endmodule
module removed;
endmodule
class retained_class;
  int value;
endclass
class removed_class;
  int value;
endclass
primitive retained_udp(output q, input a);
  table
    0 : 0;
    1 : 1;
  endtable
endprimitive
primitive removed_udp(output q, input a);
  table
    0 : 1;
    1 : 0;
  endtable
endprimitive
)");
    auto selected = select(object, "retained");
    const auto class_object = select(object, "retained_class");
    const auto udp_object = select(object, "retained_udp");
    selected.active_units.push_back(class_object.active_units.front());
    selected.active_units.push_back(udp_object.active_units.front());
    fsim::diagnostic::Engine diagnostics;
    auto loaded = detail::load_workspace_objects(std::array { selected }, diagnostics);
    if (!loaded) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(loaded && !diagnostics.has_error());
    assert(loaded->find_unit(
        fsim::semantic::UnitKind::verilog_module, "work", "retained"));
    assert(!loaded->find_unit(
        fsim::semantic::UnitKind::verilog_module, "work", "removed"));
    assert(loaded->systemverilog_hir.classes().size() == 1U);
    assert(loaded->systemverilog_hir.classes().front().name == "retained_class");
    assert(loaded->systemverilog_hir.udps().size() == 1U);
    assert(loaded->systemverilog_hir.udps().front().name == "retained_udp");

    // Independent class objects may originate in the same compilation unit.
    // Their private synthetic owner scopes must not collide after splitting.
    const std::array class_objects {
        class_object, select(object, "removed_class")
    };
    diagnostics.clear();
    auto classes = detail::load_workspace_objects(class_objects, diagnostics);
    if (!classes) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(classes && !diagnostics.has_error());
    assert(classes->systemverilog_hir.classes().size() == 2U);

    auto stale = selected;
    stale.active_units.front().name = "absent";
    diagnostics.clear();
    assert(!detail::load_workspace_objects(std::array { stale }, diagnostics));
    assert(diagnostics.has_error());
    diagnostics.clear();
    assert(!detail::load_workspace_objects(
        std::array { selected, selected }, diagnostics));
    assert(diagnostics.has_error());
}

void check_managed_order_boundary(const std::filesystem::path& root)
{
    const auto first = compile(root, "ordered-first",
        "module ordered_first; endmodule\n");
    const auto second = compile(root, "ordered-second",
        "module ordered_second; endmodule\n");
    const auto next_catalog = compile(root, "ordered-next",
        "module ordered_next; endmodule\n");
    fsim::diagnostic::Engine diagnostics;
    const std::array manual { second, first };
    const auto loaded_manual = detail::load_workspace_objects(manual, diagnostics);
    assert(loaded_manual && !diagnostics.has_error()
        && loaded_manual->objects.size() == 2U
        && loaded_manual->objects[0].directory == second.path
        && loaded_manual->objects[1].directory == first.path);

    auto ranked_first = first;
    auto ranked_second = second;
    auto ranked_next = next_catalog;
    ranked_first.catalog_rank = 0U;
    ranked_second.catalog_rank = 0U;
    ranked_next.catalog_rank = 1U;
    diagnostics.clear();
    const std::array forward { ranked_first, ranked_second, ranked_next };
    const std::array reversed { ranked_second, ranked_first, ranked_next };
    const auto loaded_forward = detail::load_workspace_objects(
        forward, diagnostics);
    assert(loaded_forward && !diagnostics.has_error());
    diagnostics.clear();
    const auto loaded_reversed = detail::load_workspace_objects(
        reversed, diagnostics);
    assert(loaded_reversed && !diagnostics.has_error()
        && loaded_forward->objects.size() == 3U
        && loaded_reversed->objects.size() == 3U);
    for (std::size_t index = 0; index < 3U; ++index) {
        assert(loaded_forward->objects[index].metadata_digest
            == loaded_reversed->objects[index].metadata_digest);
    }
    assert(loaded_forward->objects.back().directory == next_catalog.path
        && loaded_reversed->objects.back().directory == next_catalog.path);
}

fsim::app::TraceArchiveSnapshot trace_snapshot(
    const std::filesystem::path& root,
    const fsim::app::TraceControlSurface surface,
    const fsim::app::TraceLifecycle lifecycle,
    const std::uint64_t generation)
{
    fsim::app::TraceControlRequest request;
    request.surface = surface;
    request.phase = fsim::app::TraceControlPhase::Compile;
    request.output = root / "waves.fst";
    request.format = fsim::project::TraceFormat::fst;
    request.compression = fsim::project::TraceCompression::none;
    request.selection = { "shared.*" };
    request.lifecycle = lifecycle;
    request.generation = generation;
    const auto applied = fsim::app::apply_trace_control(std::move(request));
    assert(applied.ok());
    return fsim::app::make_trace_archive_snapshot(
        *applied.application, root);
}

void attach_trace_archive(const detail::WorkspaceObjectSelection& object,
    const fsim::app::TraceArchiveSnapshot& snapshot)
{
    fsim::diagnostic::Engine diagnostics;
    auto metadata = fsim::artifact::load_object_metadata(object.path, diagnostics);
    assert(metadata && !diagnostics.has_error());
    const auto encoded = fsim::app::encode_trace_archive(
        snapshot, fsim::app::TraceArchiveKind::Object);
    assert(encoded.ok());
    metadata->trace_archive = fsim::app::trace_archive_hex(encoded.archive);
    metadata->compilation_digest
        = fsim::artifact::compute_object_compilation_digest(*metadata);
    const auto path = object.path / fsim::artifact::kObjectMetadataFilename;
    std::filesystem::permissions(path,
        std::filesystem::perms::owner_write,
        std::filesystem::perm_options::add);
    write_file(path, fsim::artifact::serialize_object_metadata(*metadata));
}

void check_trace_replay_class_and_representative(
    const std::filesystem::path& root)
{
    const auto first = compile(root, "trace-first",
        "module trace_first; endmodule\n");
    const auto second = compile(root, "trace-second",
        "module trace_second; endmodule\n");
    const auto first_snapshot = trace_snapshot(root,
        fsim::app::TraceControlSurface::ProjectCli,
        fsim::app::TraceLifecycle::Configured, 1U);
    const auto second_snapshot = trace_snapshot(root,
        fsim::app::TraceControlSurface::Tcl,
        fsim::app::TraceLifecycle::Configured, 7U);
    assert(fsim::app::trace_archive_profiles_compatible(
        first_snapshot, second_snapshot));
    attach_trace_archive(first, first_snapshot);
    attach_trace_archive(second, second_snapshot);
    fsim::diagnostic::Engine diagnostics;
    const auto forward = detail::load_workspace_objects(
        std::array { first, second }, diagnostics);
    assert(forward && !diagnostics.has_error() && forward->trace_archive);
    diagnostics.clear();
    const auto reversed = detail::load_workspace_objects(
        std::array { second, first }, diagnostics);
    assert(reversed && !diagnostics.has_error() && reversed->trace_archive
        && *forward->trace_archive == *reversed->trace_archive);

    const auto disabled = trace_snapshot(root,
        fsim::app::TraceControlSurface::ProjectCli,
        fsim::app::TraceLifecycle::Disabled, 1U);
    assert(fsim::app::trace_archive_profiles_compatible(
        first_snapshot, disabled));
    attach_trace_archive(second, disabled);
    diagnostics.clear();
    assert(!detail::load_workspace_objects(
        std::array { first, second }, diagnostics));
    assert(std::ranges::any_of(diagnostics.diagnostics(),
        [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-TRACE-ARCHIVE-003";
        }));
}

void mark_uvm_release(const detail::WorkspaceObjectSelection& object)
{
    fsim::diagnostic::Engine diagnostics;
    auto metadata = fsim::artifact::load_object_metadata(object.path, diagnostics);
    assert(metadata && !diagnostics.has_error());
    metadata->uvm_release = fsim::project::to_string(
        fsim::project::SystemVerilogUvmRelease::uvm_1_2);
    metadata->compilation_digest
        = fsim::artifact::compute_object_compilation_digest(*metadata);
    const auto path = object.path / fsim::artifact::kObjectMetadataFilename;
    std::filesystem::permissions(path, std::filesystem::perms::owner_write,
        std::filesystem::perm_options::add);
    write_file(path, fsim::artifact::serialize_object_metadata(*metadata));
}

void check_split_uvm_environment(const std::filesystem::path& root)
{
    const auto package = compile(root, "uvm-package", R"(
package uvm_pkg;
  class uvm_object;
  endclass
endpackage
)");
    const auto consumer = compile(root, "uvm-consumer",
        "module uvm_consumer; endmodule\n");
    mark_uvm_release(package);
    mark_uvm_release(consumer);
    fsim::diagnostic::Engine diagnostics;
    assert(!detail::load_object_workspace(std::array { consumer.path }, diagnostics));
    assert(diagnostics.has_error());
    diagnostics.clear();
    auto loaded = detail::load_workspace_objects(
        std::array { package, consumer }, diagnostics);
    if (!loaded) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(loaded && !diagnostics.has_error());
    assert(loaded->systemverilog_uvm_provenance.release
        == fsim::project::SystemVerilogUvmRelease::uvm_1_2);
    diagnostics.clear();
    assert(!detail::load_workspace_objects(std::array { consumer }, diagnostics));
    diagnostics.clear();
    assert(detail::load_workspace_objects(
        std::array { consumer }, diagnostics, false));
    assert(!diagnostics.has_error());
}

void check_compilation_unit_partition(const std::filesystem::path& root,
    const bool mutable_state)
{
    const auto declarations = root / "declarations.sv";
    const auto modules = root / "modules.sv";
    write_file(declarations, mutable_state
            ? "module carrier; int shared_counter = 0; endmodule\n"
            : "module carrier; typedef int shared_type; "
              "localparam int shared_limit = 3; endmodule\n");
    write_file(modules, "module first; endmodule\nmodule second; endmodule\n");
    fsim::project::Config config;
    config.base_directory = root;
    config.project.name = "compilation-unit-partition";
    config.build.cache_path = root / ".fsim" / "cache";
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.compilation_unit = "source-set";
    sources.files = { declarations, modules };
    config.source_sets.push_back(std::move(sources));
    fsim::diagnostic::Engine diagnostics;
    auto checked = detail::check_project_for_object(config, diagnostics);
    if (!checked) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(checked && !diagnostics.has_error());
    // Bare compilation-unit variables and typedefs are not accepted by the
    // frontend yet. Exercise the compiled-HIR partition boundary by changing
    // this otherwise ordinary declaration owner into a private CU container.
    const auto carrier = checked->find_unit(
        fsim::semantic::UnitKind::verilog_module, "work", "carrier");
    assert(carrier);
    const auto carrier_id = carrier->identity->id;
    auto records = checked->semantics.records();
    records.units[carrier_id.value()].kind
        = fsim::semantic::UnitKind::systemverilog_compilation_unit;
    auto model = fsim::semantic::Model::from_records(std::move(records));
    assert(model);
    checked->semantics = std::move(*model);
    auto& hir_units = checked->systemverilog_hir.mutable_units();
    const auto hir_carrier = std::ranges::find(
        hir_units, carrier_id, &fsim::semantic::sv::Unit::id);
    assert(hir_carrier != hir_units.end());
    hir_carrier->kind = fsim::semantic::sv::UnitKind::compilation_unit;
    fsim::semantic::refresh_compiled_design_metadata(*checked);

    const auto groups = detail::partition_workspace_objects(*checked, config, diagnostics);
    assert(groups && !diagnostics.has_error());
    assert(groups->size() == (mutable_state ? 1U : 2U));
    std::string error;
    const auto module_source = fsim::app::workspace::source_identity(modules, error);
    assert(module_source && error.empty());
    std::vector<std::string> supporting;
    for (const auto& dependency : detail::vhdl_package_dependencies(*checked)) {
        const auto library = dependency.package.substr(0, dependency.package.find('.'));
        if (std::ranges::find(supporting, library) == supporting.end()) {
            supporting.push_back(library);
        }
    }
    std::vector<detail::WorkspaceObjectSelection> selected;
    for (const auto& group : *groups) {
        assert(group.sources.size() == 2U);
        assert(group.unit_sources.size() == (mutable_state ? 2U : 1U));
        for (const auto& [identity, source] : group.unit_sources) {
            assert(!identity.empty() && source == *module_source);
        }
        auto projection = fsim::semantic::extract_compiled_objects(*checked,
            group.units, group.classes, group.primitives, supporting);
        assert(projection.ok());
        const auto path = root / ("selected-" + std::to_string(selected.size()) + ".fsimobj");
        const auto published = detail::publish_workspace_object(config, *checked,
            std::move(*projection.design), path, diagnostics);
        if (!published) {
            fsim::diagnostic::print_text(std::cerr, diagnostics);
        }
        assert(published && !diagnostics.has_error());
        const auto metadata = fsim::artifact::load_object_metadata(path, diagnostics);
        assert(metadata && !diagnostics.has_error());
        selected.push_back({ path, metadata->units });
    }
    std::filesystem::remove(declarations);
    std::filesystem::remove(modules);
    auto loaded = detail::load_workspace_objects(selected, diagnostics);
    if (!loaded) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(loaded && !diagnostics.has_error());
    assert(loaded->find_unit(fsim::semantic::UnitKind::verilog_module, "work", "first"));
    assert(loaded->find_unit(fsim::semantic::UnitKind::verilog_module, "work", "second"));
    if (mutable_state) {
        assert(std::ranges::count_if(loaded->systemverilog_hir.declarations(),
                   [](const auto& declaration) { return declaration.name == "shared_counter"; })
            == 1);
    } else {
        assert(std::ranges::count_if(loaded->systemverilog_hir.declarations(),
                   [](const auto& declaration) { return declaration.name == "shared_limit"; })
            == 2);
    }
}

void check_global_class_partition(const std::filesystem::path& root)
{
    const auto source = root / "classes.sv";
    write_file(source, R"(
class Outer;
  int value;
  class Inner;
    int nested_value;
  endclass
endclass
class Other extends Outer;
  int another_value;
endclass
)");
    fsim::project::Config config;
    config.base_directory = root;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.files = { source };
    config.source_sets.push_back(std::move(sources));
    fsim::diagnostic::Engine diagnostics;
    const auto checked = detail::check_project_for_object(config, diagnostics);
    if (!checked) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(checked && !diagnostics.has_error());
    const auto groups = detail::partition_workspace_objects(*checked, config, diagnostics);
    assert(groups && !diagnostics.has_error() && groups->size() == 2U);
    std::size_t class_count { };
    for (const auto& group : *groups) {
        class_count += group.classes.size();
        const bool owns_outer = std::ranges::any_of(group.classes,
            [](const auto& identity) { return identity.ends_with("::Outer"); });
        assert(group.classes.size() == (owns_outer ? 2U : 1U));
        if (owns_outer) {
            assert(std::ranges::any_of(group.classes,
                [](const auto& identity) { return identity.ends_with("::Outer::Inner"); }));
        }
    }
    assert(class_count == 3U);
}

void check_legacy_vhdl_identifier_hydration(
    const std::filesystem::path& root)
{
    const auto basic = compile_vhdl(root / "basic-source", "basic", R"(
entity ctx is end entity;
)");
    const auto extended = compile_vhdl(root / "extended-source", "extended", R"(
entity \ctx\ is end entity;
)");

    workspace::Store store { root / "workspace" };
    auto fixture = make_legacy_catalog(store, { basic, extended });
    detail::WorkspaceCatalogs catalogs { fixture.catalog };
    for (const auto& artifact : catalogs.front().artifacts) {
        assert(artifact.units.size() == 1U);
        assert(artifact.units.front().unit.name_form
            == fsim::library::VhdlIdentifierForm::unknown);
        assert(artifact.units.front().unit.primary_name.empty());
        assert(artifact.units.front().unit.primary_name_form
            == fsim::library::VhdlIdentifierForm::unknown);
    }

    fsim::project::Config selector_config;
    selector_config.project.tops.push_back({ "vhdl:work.ctx", { } });
    fsim::diagnostic::Engine selector_diagnostics;
    const auto selected = detail::select_workspace_design(
        selector_config, store, catalogs, selector_diagnostics);
    if (!selected) {
        fsim::diagnostic::print_text(std::cerr, selector_diagnostics);
    }
    assert(selected && !selector_diagnostics.has_error());
    assert(selected->objects.size() == 1U);
    assert(selected->objects.front().active_units.size() == 1U);
    assert(selected->objects.front().active_units.front().name_form
        == fsim::library::VhdlIdentifierForm::basic);
    assert(selected->objects.front().active_units.front().primary_name.empty());
    assert(selected->objects.front().active_units.front().primary_name_form
        == fsim::library::VhdlIdentifierForm::unknown);
    assert(selected->catalogs.size() == 1U);
    assert(selected->catalogs.front().artifacts.size() == 1U);
    assert(selected->catalogs.front().artifacts.front().units.front().unit.name_form
        == fsim::library::VhdlIdentifierForm::basic);
    // Selection hydrates a private snapshot; callers holding the legacy
    // catalog do not observe partially upgraded state.
    assert(catalogs.front().artifacts[0].units.front().unit.name_form
        == fsim::library::VhdlIdentifierForm::unknown);
    assert(catalogs.front().artifacts[1].units.front().unit.name_form
        == fsim::library::VhdlIdentifierForm::unknown);

    fsim::project::Config basic_config;
    basic_config.project.tops.push_back({ "vhdl:work.ctx", { } });
    fsim::diagnostic::Engine basic_diagnostics;
    assert(detail::resolve_workspace_tops(basic_config, store, catalogs,
        basic_diagnostics));
    assert(!basic_diagnostics.has_error());
    assert(basic_config.project.tops.front().target == "vhdl:work.ctx");
    assert(catalogs.front().artifacts[0].units.front().unit.name_form
        == fsim::library::VhdlIdentifierForm::basic);
    assert(catalogs.front().artifacts[1].units.front().unit.name_form
        == fsim::library::VhdlIdentifierForm::extended);
    for (const auto& artifact : catalogs.front().artifacts) {
        assert(artifact.units.front().unit.primary_name.empty());
        assert(artifact.units.front().unit.primary_name_form
            == fsim::library::VhdlIdentifierForm::unknown);
    }

    fsim::project::Config extended_config;
    extended_config.project.tops.push_back({ R"(vhdl:work.\ctx\)", { } });
    fsim::diagnostic::Engine extended_diagnostics;
    assert(detail::resolve_workspace_tops(extended_config, store, catalogs,
        extended_diagnostics));
    assert(!extended_diagnostics.has_error());
    assert(extended_config.project.tops.front().target
        == R"(vhdl:work.\ctx\)");
}

void check_legacy_vhdl_primary_identifier_hydration(
    const std::filesystem::path& root)
{
    const auto object = compile_vhdl(root / "source", "primary-hydration", R"(
entity primary_shape is end entity;
architecture rtl of primary_shape is
begin
end architecture;
)");
    assert(object.active_units.size() == 2U);
    const auto compiled_architecture = std::ranges::find_if(
        object.active_units, [](const auto& unit) {
            return unit.kind == "architecture";
        });
    assert(compiled_architecture != object.active_units.end());
    assert(compiled_architecture->primary_name == "primary_shape");
    assert(compiled_architecture->primary_name_form
        == fsim::library::VhdlIdentifierForm::basic);

    workspace::Store store { root / "workspace" };
    auto fixture = make_legacy_catalog(store, { object });
    detail::WorkspaceCatalogs catalogs { fixture.catalog };
    assert(catalogs.front().artifacts.size() == 1U);
    const auto legacy_architecture = std::ranges::find_if(
        catalogs.front().artifacts.front().units, [](const auto& unit) {
            return unit.unit.kind == "architecture";
        });
    assert(legacy_architecture
        != catalogs.front().artifacts.front().units.end());
    assert(legacy_architecture->unit.primary_name == "primary_shape");
    assert(legacy_architecture->unit.name_form
        == fsim::library::VhdlIdentifierForm::unknown);
    assert(legacy_architecture->unit.primary_name_form
        == fsim::library::VhdlIdentifierForm::unknown);

    fsim::project::Config config;
    config.project.tops.push_back({ "vhdl:work.primary_shape", { } });
    fsim::diagnostic::Engine diagnostics;
    assert(detail::resolve_workspace_tops(config, store, catalogs, diagnostics));
    assert(!diagnostics.has_error());
    const auto hydrated_architecture = std::ranges::find_if(
        catalogs.front().artifacts.front().units, [](const auto& unit) {
            return unit.unit.kind == "architecture";
        });
    assert(hydrated_architecture
        != catalogs.front().artifacts.front().units.end());
    assert(hydrated_architecture->unit.name_form
        == fsim::library::VhdlIdentifierForm::basic);
    assert(hydrated_architecture->unit.primary_name == "primary_shape");
    assert(hydrated_architecture->unit.primary_name_form
        == fsim::library::VhdlIdentifierForm::basic);
}

void check_legacy_vhdl_primary_form_conflict_fails_closed(
    const std::filesystem::path& root)
{
    auto object = compile_vhdl(root / "source", "primary-form-conflict", R"(
entity primary_shape is end entity;
architecture rtl of primary_shape is
begin
end architecture;
)");
    fsim::diagnostic::Engine metadata_diagnostics;
    auto metadata = fsim::artifact::load_object_metadata(
        object.path, metadata_diagnostics);
    assert(metadata && !metadata_diagnostics.has_error());
    const auto architecture = std::ranges::find_if(
        metadata->units, [](const auto& unit) {
            return unit.kind == "architecture";
        });
    assert(architecture != metadata->units.end());
    assert(architecture->primary_name == "primary_shape");
    assert(architecture->primary_name_form
        == fsim::library::VhdlIdentifierForm::basic);
    architecture->primary_name_form
        = fsim::library::VhdlIdentifierForm::extended;
    metadata->compilation_digest
        = fsim::artifact::compute_object_compilation_digest(*metadata);
    write_object_metadata(object.path, *metadata);

    fsim::diagnostic::Engine diagnostics;
    assert(!detail::load_workspace_objects(std::array { object }, diagnostics));
    assert(std::ranges::any_of(diagnostics.diagnostics(), [](const auto& item) {
        return item.code == "FSIM-ART-0005"
            && item.message.find(
                ".fsimobj unit identity does not match its compiled-HIR index")
                != std::string::npos;
    }));
}

void check_legacy_vhdl_known_form_conflict_fails_closed(
    const std::filesystem::path& root)
{
    auto object = compile_vhdl(root / "source", "form-conflict", R"(
entity form_conflict is end entity;
)");
    fsim::diagnostic::Engine metadata_diagnostics;
    auto metadata = fsim::artifact::load_object_metadata(
        object.path, metadata_diagnostics);
    assert(metadata && !metadata_diagnostics.has_error());
    assert(metadata->units.size() == 1U);
    assert(metadata->units.front().name_form
        == fsim::library::VhdlIdentifierForm::basic);
    metadata->units.front().name_form
        = fsim::library::VhdlIdentifierForm::extended;
    metadata->compilation_digest
        = fsim::artifact::compute_object_compilation_digest(*metadata);
    write_object_metadata(object.path, *metadata);

    fsim::diagnostic::Engine diagnostics;
    assert(!detail::load_workspace_objects(std::array { object }, diagnostics));
    assert(has_diagnostic_code(diagnostics, "FSIM-ART-0005"));
    assert(std::ranges::any_of(diagnostics.diagnostics(), [](const auto& item) {
        return item.code == "FSIM-ART-0005"
            && item.message.find(
                ".fsimobj unit identity does not match its compiled-HIR index")
                != std::string::npos;
    }));
}

void check_legacy_vhdl_identifier_collision_fails_closed(
    const std::filesystem::path& root)
{
    const auto collision = compile_vhdl(root / "collision-source", "collision", R"(
entity ctx is end entity;
entity \ctx\ is end entity;
)");
    workspace::Store store { root / "workspace" };
    auto fixture = make_legacy_catalog(store, { collision });
    detail::WorkspaceCatalogs catalogs { fixture.catalog };
    fsim::project::Config config;
    config.project.tops.push_back({ "vhdl:work.ctx", { } });
    fsim::diagnostic::Engine diagnostics;
    assert(!detail::resolve_workspace_tops(config, store, catalogs, diagnostics));
    assert(diagnostics.has_error());
    assert(has_diagnostic_code(diagnostics, "FSIM-WORKSPACE-004"));
    assert(std::ranges::any_of(diagnostics.diagnostics(), [](const auto& item) {
        return item.message.find("ambiguous") != std::string::npos;
    }));
    fsim::project::Config selector_config;
    selector_config.project.tops.push_back({ "vhdl:work.ctx", { } });
    diagnostics.clear();
    assert(!detail::select_workspace_design(
        selector_config, store, catalogs, diagnostics));
    assert(diagnostics.has_error());
    assert(has_diagnostic_code(diagnostics, "FSIM-WORKSPACE-004"));
    assert(std::ranges::any_of(diagnostics.diagnostics(), [](const auto& item) {
        return item.message.find("ambiguous") != std::string::npos;
    }));
}

void check_missing_legacy_vhdl_hir_fails_closed(
    const std::filesystem::path& root)
{
    const auto basic = compile_vhdl(root / "source", "missing-hir", R"(
entity missing_hir is end entity;
)");
    workspace::Store store { root / "workspace" };
    auto fixture = make_legacy_catalog(store, { basic });
    detail::WorkspaceCatalogs catalogs { fixture.catalog };
    std::string error;
    const auto path = store.artifact_path(catalogs.front().location,
        catalogs.front().artifacts.front(), error);
    assert(path && error.empty());
    fsim::diagnostic::Engine metadata_diagnostics;
    const auto metadata = fsim::artifact::load_object_metadata(
        *path, metadata_diagnostics);
    assert(metadata && !metadata_diagnostics.has_error());
    assert(!metadata->compiled_hir_artifact.empty());
    std::filesystem::remove(*path / metadata->compiled_hir_artifact);

    fsim::project::Config config;
    config.project.tops.push_back({ "vhdl:work.missing_hir", { } });
    fsim::diagnostic::Engine diagnostics;
    assert(!detail::resolve_workspace_tops(config, store, catalogs, diagnostics));
    assert(diagnostics.has_error());
    assert(config.project.tops.front().target == "vhdl:work.missing_hir");
    fsim::project::Config selector_config;
    selector_config.project.tops.push_back({ "vhdl:work.missing_hir", { } });
    diagnostics.clear();
    assert(!detail::select_workspace_design(
        selector_config, store, catalogs, diagnostics));
    assert(diagnostics.has_error());
}

constexpr std::string_view extended_package = R"(
package \Pkg\ is
  constant value : integer := 7;
end package;
)";

constexpr std::string_view basic_package = R"(
package pkg is
  constant value : integer := 7;
end package;
)";

constexpr std::string_view extended_callable_package = R"(
package \CallablePkg\ is
  constant value : integer := 7;
  function fetch(index : integer) return integer;
end package;
package body \CallablePkg\ is
  function fetch(index : integer) return integer is
  begin
    return value + index;
  end function;
end package body;
)";

constexpr std::string_view extended_package_consumer = R"(
library work;
use work.\Pkg\.all;
entity client is end entity;
architecture rtl of client is
  constant copied : integer := value;
begin
end architecture;
)";

std::optional<std::vector<workspace::UnitDependency>> dependencies_for_client(
    const std::filesystem::path& root, workspace::Store& store,
    const detail::WorkspaceObjectSelection& imported_object,
    const detail::WorkspaceCatalogs& catalogs,
    fsim::diagnostic::Engine& diagnostics)
{
    const std::array imported_objects { imported_object };
    auto imported = detail::load_workspace_objects(imported_objects, diagnostics);
    assert(imported && !diagnostics.has_error());
    auto checked = check_vhdl_with_imported(root, "client",
        extended_package_consumer, *imported, diagnostics);
    if (!checked) {
        return std::nullopt;
    }
    const auto owner = checked->find_unit(
        fsim::semantic::UnitKind::vhdl_architecture, "work", "client", "rtl");
    assert(owner && owner->identity);
    const std::array owners { owner->identity->id };
    return detail::workspace_dependencies(
        store, *checked, owners, catalogs, diagnostics);
}

void check_vhdl_package_reference_targeting(
    const std::filesystem::path& root)
{
    const auto basic = compile_vhdl(root / "basic-provider", "basic-provider",
        basic_package);
    const auto extended = compile_vhdl(root / "extended-provider",
        "extended-provider", extended_package);
    const std::array imported_objects { basic, extended };
    fsim::diagnostic::Engine import_diagnostics;
    auto imported = detail::load_workspace_objects(
        imported_objects, import_diagnostics);
    assert(imported && !import_diagnostics.has_error());

    const auto verify_target = [&](const std::string& project,
                                   const std::string_view owner_name,
                                   const fsim::semantic::CompiledDesign& design,
                                   const std::string_view source,
                                   const std::string_view reference_name,
                                   const std::string_view expected_unit_name,
                                   const bool expected_extended,
                                   const fsim::semantic::UnitKind
                                       expected_reference_owner_kind) {
        fsim::diagnostic::Engine diagnostics;
        const auto checked = check_vhdl_with_imported(root / project,
            project, source, design, diagnostics);
        if (!checked) {
            fsim::diagnostic::print_text(std::cerr, diagnostics);
        }
        assert(checked && !diagnostics.has_error());
        const auto owner = checked->find_unit(
            fsim::semantic::UnitKind::vhdl_architecture, "work",
            owner_name, "rtl");
        assert(owner && owner->identity);
        const auto reference_owner = expected_reference_owner_kind
                == fsim::semantic::UnitKind::vhdl_entity
            ? checked->find_unit(fsim::semantic::UnitKind::vhdl_entity,
                  "work", owner_name)
            : owner;
        assert(reference_owner && reference_owner->identity);
        const auto reference_owner_id = reference_owner->identity->id;
        const auto package_references = std::ranges::count_if(
            checked->references(), [&](const auto& reference) {
                return reference.owner == reference_owner_id
                    && reference.kind
                        == fsim::semantic::CompiledReferenceKind::package;
            });
        assert(package_references == 1U);
        const auto reference = std::ranges::find_if(
            checked->references(), [&](const auto& candidate) {
                return candidate.owner == reference_owner_id
                    && candidate.kind
                        == fsim::semantic::CompiledReferenceKind::package;
            });
        assert(reference != checked->references().end());
        assert(reference->name == reference_name);
        assert(reference->target);
        const auto target = checked->find_unit(*reference->target);
        assert(target && target->vhdl != nullptr);
        assert(target->vhdl->kind
            == fsim::semantic::vhdl::UnitKind::package);
        assert(target->vhdl->name == expected_unit_name);
        assert(target->vhdl->extended_name == expected_extended);
    };

    verify_target("extended-use", "extended_use_client", *imported, R"(
library work;
use work.\Pkg\.all;
entity extended_use_client is end entity;
architecture rtl of extended_use_client is
  constant copied : integer := value;
begin
end architecture;
)", R"(\Pkg\)", "Pkg", true, fsim::semantic::UnitKind::vhdl_entity);
    verify_target("basic-use", "basic_use_client", *imported, R"(
library work;
use work.pkg.all;
entity basic_use_client is end entity;
architecture rtl of basic_use_client is
  constant copied : integer := value;
begin
end architecture;
)", "pkg", "pkg", false, fsim::semantic::UnitKind::vhdl_entity);
    verify_target("basic-selected", "basic_selected_client", *imported, R"(
library work;
entity basic_selected_client is end entity;
architecture rtl of basic_selected_client is
  constant copied : integer := work.pkg.value;
begin
end architecture;
)", "pkg", "pkg", false, fsim::semantic::UnitKind::vhdl_architecture);
    verify_target("extended-selected", "selected_client", *imported, R"(
library work;
entity selected_client is end entity;
architecture rtl of selected_client is
  constant copied : integer := work.\Pkg\.value;
begin
end architecture;
)", R"(\Pkg\)", "Pkg", true, fsim::semantic::UnitKind::vhdl_architecture);

    const auto callable = compile_vhdl(root / "callable-provider",
        "callable-provider", extended_callable_package);
    const std::array callable_objects { callable };
    fsim::diagnostic::Engine callable_diagnostics;
    auto callable_design = detail::load_workspace_objects(
        callable_objects, callable_diagnostics);
    assert(callable_design && !callable_diagnostics.has_error());
    verify_target("extended-selected-call", "selected_call_client",
        *callable_design, R"(
library work;
entity selected_call_client is end entity;
architecture rtl of selected_call_client is
  constant copied : integer := work.\CallablePkg\.fetch(0);
begin
end architecture;
)", R"(\CallablePkg\)", "CallablePkg", true, fsim::semantic::UnitKind::vhdl_architecture);
}

void check_legacy_vhdl_dependency_hydration(const std::filesystem::path& root)
{
    const auto provider = compile_vhdl(root / "provider", "extended-provider",
        extended_package);
    workspace::Store store { root / "workspace" };
    auto fixture = make_legacy_catalog(store, { provider });
    detail::WorkspaceCatalogs catalogs { fixture.catalog };
    assert(catalogs.front().artifacts.front().units.front().unit.name_form
        == fsim::library::VhdlIdentifierForm::unknown);

    fsim::diagnostic::Engine diagnostics;
    auto dependencies = dependencies_for_client(root / "consumer", store,
        provider, catalogs, diagnostics);
    if (!dependencies) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(dependencies && !diagnostics.has_error());
    assert(std::ranges::any_of(*dependencies, [](const auto& dependency) {
        return dependency.library == "work" && dependency.unit.name == "Pkg"
            && dependency.unit.name_form
            == fsim::library::VhdlIdentifierForm::extended;
    }));
    assert(catalogs.front().artifacts.front().units.front().unit.name_form
        == fsim::library::VhdlIdentifierForm::unknown);

    workspace::ArtifactRecord consumer;
    consumer.id = "consumer-client";
    consumer.fingerprint = "consumer-fingerprint";
    fsim::library::UnitIndexEntry consumer_unit;
    consumer_unit.language = "vhdl";
    consumer_unit.kind = "entity";
    consumer_unit.name = "client";
    consumer_unit.name_form = fsim::library::VhdlIdentifierForm::basic;
    consumer.units.push_back({ std::move(consumer_unit), "consumer.vhd" });
    consumer.dependencies = *dependencies;
    catalogs.front().artifacts.push_back(std::move(consumer));
    fsim::project::Config config;
    config.project.tops.push_back({ "vhdl:work.client", { } });
    diagnostics.clear();
    assert(detail::resolve_workspace_tops(config, store, catalogs, diagnostics));
    assert(!diagnostics.has_error());
    assert(catalogs.front().artifacts.front().units.front().unit.name_form
        == fsim::library::VhdlIdentifierForm::extended);
    assert(detail::validate_workspace_dependencies(catalogs, diagnostics));
    assert(!diagnostics.has_error());
}

void check_legacy_vhdl_dependency_mismatch_is_not_matched(
    const std::filesystem::path& root)
{
    const auto basic = compile_vhdl(root / "basic-provider", "basic-provider",
        basic_package);
    const auto extended = compile_vhdl(root / "extended-provider", "extended-provider",
        extended_package);
    const std::array imported_objects { basic, extended };
    fsim::diagnostic::Engine diagnostics;
    auto imported = detail::load_workspace_objects(imported_objects, diagnostics);
    assert(imported && !diagnostics.has_error());
    auto checked = check_vhdl_with_imported(root / "consumer", "client",
        extended_package_consumer, *imported, diagnostics);
    assert(checked && !diagnostics.has_error());
    const auto owner = checked->find_unit(
        fsim::semantic::UnitKind::vhdl_architecture, "work", "client", "rtl");
    assert(owner && owner->identity);
    const std::array owners { owner->identity->id };

    workspace::Store store { root / "workspace" };
    auto fixture = make_legacy_catalog(store, { basic });
    detail::WorkspaceCatalogs catalogs { fixture.catalog };
    const auto dependencies = detail::workspace_dependencies(
        store, *checked, owners, catalogs, diagnostics);
    assert(dependencies && !diagnostics.has_error());
    assert(std::ranges::none_of(*dependencies, [](const auto& dependency) {
        return dependency.library == "work" && dependency.unit.name == "pkg";
    }));
    assert(catalogs.front().artifacts.front().units.front().unit.name_form
        == fsim::library::VhdlIdentifierForm::unknown);
}

void check_ambiguous_legacy_vhdl_dependency_fails_closed(
    const std::filesystem::path& root)
{
    const auto collision = compile_vhdl(root / "collision-provider", "collision-provider", R"(
package same is
  constant basic_value : integer := 1;
end package;
package \same\ is
  constant extended_value : integer := 2;
end package;
)");
    const auto extended_provider = compile_vhdl(root / "extended-provider",
        "extended-provider", R"(
package \same\ is
  constant extended_value : integer := 2;
end package;
)");
    fsim::diagnostic::Engine diagnostics;
    const std::array imported_objects { extended_provider };
    auto imported = detail::load_workspace_objects(imported_objects, diagnostics);
    assert(imported && !diagnostics.has_error());
    auto checked = check_vhdl_with_imported(root / "consumer", "client", R"(
library work;
use work.\same\.all;
entity client is end entity;
architecture rtl of client is
  constant copied : integer := extended_value;
begin
end architecture;
)",
        *imported, diagnostics);
    assert(checked && !diagnostics.has_error());
    const auto owner = checked->find_unit(
        fsim::semantic::UnitKind::vhdl_architecture, "work", "client", "rtl");
    assert(owner && owner->identity);
    const std::array owners { owner->identity->id };

    workspace::Store store { root / "workspace" };
    auto fixture = make_legacy_catalog(store, { collision });
    detail::WorkspaceCatalogs catalogs { fixture.catalog };
    const auto dependencies = detail::workspace_dependencies(
        store, *checked, owners, catalogs, diagnostics);
    assert(!dependencies);
    assert(diagnostics.has_error());
    assert(has_diagnostic_code(diagnostics, "FSIM-WORKSPACE-004"));
    assert(std::ranges::all_of(catalogs.front().artifacts.front().units,
        [](const auto& owned) {
            return owned.unit.name_form
                == fsim::library::VhdlIdentifierForm::unknown;
        }));
}

void check_missing_legacy_vhdl_dependency_hir_fails_closed(
    const std::filesystem::path& root)
{
    const auto provider = compile_vhdl(root / "provider", "extended-provider",
        extended_package);
    fsim::diagnostic::Engine diagnostics;
    const std::array imported_objects { provider };
    auto imported = detail::load_workspace_objects(imported_objects, diagnostics);
    assert(imported && !diagnostics.has_error());
    auto checked = check_vhdl_with_imported(root / "consumer", "client",
        extended_package_consumer, *imported, diagnostics);
    assert(checked && !diagnostics.has_error());
    const auto owner = checked->find_unit(
        fsim::semantic::UnitKind::vhdl_architecture, "work", "client", "rtl");
    assert(owner && owner->identity);
    const std::array owners { owner->identity->id };

    workspace::Store store { root / "workspace" };
    auto fixture = make_legacy_catalog(store, { provider });
    detail::WorkspaceCatalogs catalogs { fixture.catalog };
    std::string error;
    const auto path = store.artifact_path(
        catalogs.front().location, catalogs.front().artifacts.front(), error);
    assert(path && error.empty());
    const auto metadata = fsim::artifact::load_object_metadata(
        *path, diagnostics);
    assert(metadata && !diagnostics.has_error());
    assert(!metadata->compiled_hir_artifact.empty());
    std::filesystem::remove(*path / metadata->compiled_hir_artifact);

    const auto dependencies = detail::workspace_dependencies(
        store, *checked, owners, catalogs, diagnostics);
    assert(!dependencies);
    assert(diagnostics.has_error());
    assert(catalogs.front().artifacts.front().units.front().unit.name_form
        == fsim::library::VhdlIdentifierForm::unknown);
}

} // namespace

int main()
{
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    TemporaryDirectory temporary {
        std::filesystem::temp_directory_path() / ("fsim-workspace-objects-" + unique)
    };
    check_catalog_selection(temporary.path / "selection");
    check_managed_order_boundary(temporary.path / "managed-order");
    check_trace_replay_class_and_representative(
        temporary.path / "trace-representative");
    check_split_uvm_environment(temporary.path / "uvm");
    check_compilation_unit_partition(temporary.path / "mutable-state", true);
    check_compilation_unit_partition(temporary.path / "constant-support", false);
    check_global_class_partition(temporary.path / "global-classes");
    check_legacy_vhdl_identifier_hydration(
        temporary.path / "legacy-vhdl-hydration");
    check_legacy_vhdl_primary_identifier_hydration(
        temporary.path / "legacy-vhdl-primary-hydration");
    check_legacy_vhdl_primary_form_conflict_fails_closed(
        temporary.path / "legacy-vhdl-primary-form-conflict");
    check_legacy_vhdl_known_form_conflict_fails_closed(
        temporary.path / "legacy-vhdl-known-form-conflict");
    check_legacy_vhdl_identifier_collision_fails_closed(
        temporary.path / "legacy-vhdl-collision");
    check_missing_legacy_vhdl_hir_fails_closed(
        temporary.path / "legacy-vhdl-missing-hir");
    check_vhdl_package_reference_targeting(
        temporary.path / "vhdl-package-reference-targeting");
    check_legacy_vhdl_dependency_hydration(
        temporary.path / "legacy-vhdl-dependency");
    check_legacy_vhdl_dependency_mismatch_is_not_matched(
        temporary.path / "legacy-vhdl-dependency-mismatch");
    check_ambiguous_legacy_vhdl_dependency_fails_closed(
        temporary.path / "legacy-vhdl-dependency-ambiguous");
    check_missing_legacy_vhdl_dependency_hir_fails_closed(
        temporary.path / "legacy-vhdl-dependency-missing-hir");
    return 0;
}
