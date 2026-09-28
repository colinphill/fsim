// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/trace_archive.hpp"
#include "fsim/artifact/object.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"

#include "../../src/app/application_workspace_objects.hpp"
#include "../../src/app/application_workspace.hpp"
#include "../../src/app/application_workspace_store.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

namespace detail = fsim::app::application_detail;

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
    return 0;
}
