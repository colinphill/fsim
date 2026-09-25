// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/object.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"

#include "../../src/app/application_workspace_selection.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

namespace {

namespace detail = fsim::app::application_detail;
namespace workspace = fsim::app::workspace;

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator iterator { path, error }, end;
             !error && iterator != end; iterator.increment(error)) {
            std::filesystem::permissions(iterator->path(), std::filesystem::perms::owner_all,
                std::filesystem::perm_options::add, error);
            error.clear();
        }
        std::filesystem::remove_all(path, error);
    }
};

void require(bool condition, const fsim::diagnostic::Engine& diagnostics)
{
    if (!condition) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(condition);
}

void publish_source(const std::filesystem::path& root, const std::string& name,
    const std::string& library, const fsim::project::Language language,
    const std::string& standard, const std::string_view text)
{
    const auto source = root / (name + (language == fsim::project::Language::vhdl ? ".vhd" : ".sv"));
    std::filesystem::create_directories(root);
    {
        std::ofstream output(source);
        output << text;
        assert(output);
    }
    fsim::project::Config config;
    config.base_directory = root;
    config.project.name = name;
    config.build.cache_path = root / ".fsim" / "cache";
    fsim::project::SourceSet sources;
    sources.language = language;
    sources.library = library;
    sources.standard = standard;
    sources.files = { source };
    config.source_sets.push_back(std::move(sources));
    fsim::diagnostic::Engine diagnostics;
    auto checked = detail::check_project_for_object(config, diagnostics);
    require(checked.has_value() && !diagnostics.has_error(), diagnostics);
    const auto groups = detail::partition_workspace_objects(*checked, config, diagnostics);
    require(groups.has_value() && !diagnostics.has_error(), diagnostics);
    std::vector<std::string> supporting;
    for (const auto& dependency : detail::vhdl_package_dependencies(*checked)) {
        const auto dependency_library = dependency.package.substr(0, dependency.package.find('.'));
        if (std::ranges::find(supporting, dependency_library) == supporting.end()) {
            supporting.push_back(dependency_library);
        }
    }
    workspace::Store store(root);
    std::string error;
    auto transaction = store.begin_library(library, error);
    assert(transaction && error.empty());
    std::vector<workspace::ArtifactRecord> artifacts;
    for (const auto& group : *groups) {
        auto projected = fsim::semantic::extract_compiled_objects(*checked, group.units,
            group.classes, group.primitives, supporting);
        assert(projected.ok());
        auto record = transaction->allocate_artifact(workspace::ArtifactKind::Hdl, error);
        assert(record && error.empty());
        const auto path = transaction->artifact_path(*record);
        require(detail::publish_workspace_object(config, *checked, std::move(*projected.design),
                    path, diagnostics), diagnostics);
        auto metadata = fsim::artifact::load_object_metadata(path, diagnostics);
        require(metadata.has_value() && !diagnostics.has_error(), diagnostics);
        record->sources = group.sources;
        record->fingerprint = metadata->compilation_digest;
        for (const auto& unit : metadata->units) {
            record->units.push_back({ unit, group.unit_sources.at(workspace::unit_identity(unit)) });
            if (unit.name == "stale") {
                auto missing = unit;
                missing.kind = "package";
                missing.name = "deleted_provider";
                record->dependencies.push_back({ library, std::move(missing), std::string(64, '0') });
            }
        }
        artifacts.push_back(std::move(*record));
    }
    assert(transaction->commit(std::move(artifacts), error) && error.empty());
    std::filesystem::remove(source);
}

fsim::project::Config top_config(const std::filesystem::path& root,
    const std::string& top)
{
    fsim::project::Config config;
    config.base_directory = root;
    config.project.name = "selection";
    config.project.tops = { { top, "dut" } };
    config.project.top = top;
    config.build.cache_path = root / ".fsim" / "cache";
    return config;
}

std::set<std::string> selected_names(const detail::WorkspaceDesignSelection& selection)
{
    std::set<std::string> result;
    for (const auto& catalog : selection.catalogs) {
        for (const auto& artifact : catalog.artifacts) {
            for (const auto& owned : artifact.units) {
                result.insert(catalog.location.name + '.' + owned.unit.kind + '.' + owned.unit.name);
            }
        }
    }
    return result;
}

void check_hdl_closure(const std::filesystem::path& root)
{
    publish_source(root, "hierarchy", "work", fsim::project::Language::system_verilog, "2017", R"(
primitive pass_gate(output q, input a);
  table
    0 : 0;
    1 : 1;
  endtable
endprimitive
module leaf(output wire q, input wire a);
  pass_gate gate(q, a);
endmodule
module healthy;
  wire q;
  leaf child(q, 1'b0);
endmodule
module stale;
endmodule
)");
    publish_source(root, "pending", "work", fsim::project::Language::vhdl, "2008", R"(
package unused_pending is
  function unfinished return integer;
end package;
)");
    publish_source(root, "different-standard", "unrelated", fsim::project::Language::vhdl, "2019", R"(
entity unrelated_top is end;
architecture rtl of unrelated_top is begin end;
)");
    workspace::Store store(root);
    fsim::diagnostic::Engine diagnostics;
    auto catalogs = detail::workspace_catalogs(store, nullptr, diagnostics);
    require(catalogs.has_value(), diagnostics);
    assert(!detail::validate_workspace_dependencies(*catalogs, diagnostics));
    diagnostics.clear();
    auto config = top_config(root, "sv:work.healthy");
    auto selection = detail::select_workspace_design(config, store, *catalogs, diagnostics);
    require(selection.has_value() && !diagnostics.has_error(), diagnostics);
    assert(selected_names(*selection) == (std::set<std::string> {
        "work.module.healthy", "work.module.leaf", "work.primitive.pass_gate" }));
    require(detail::validate_workspace_dependencies(selection->catalogs, diagnostics), diagnostics);
    const auto built = detail::build_workspace_objects(config, selection->objects, selection->plugins, diagnostics);
    require(built.has_value() && !diagnostics.has_error(), diagnostics);
    const auto primitive = std::ranges::find(built->design_ir.specializations(),
        std::string { "sv:work.pass_gate" }, &fsim::semantic::design::Specialization::name);
    assert(primitive != built->design_ir.specializations().end());
    const auto& primitive_owner = built->semantics.units().at(primitive->unit.value());
    assert(primitive_owner.kind == fsim::semantic::UnitKind::systemverilog_compilation_unit);
    assert(primitive_owner.name == "$unit$udp$pass_gate");
    assert(primitive_owner.source == built->systemverilog_hir.udps().front().source);

    config = top_config(root, "sv:work.stale");
    selection = detail::select_workspace_design(config, store, *catalogs, diagnostics);
    require(selection.has_value() && !diagnostics.has_error(), diagnostics);
    assert(!detail::validate_workspace_dependencies(selection->catalogs, diagnostics));
    assert(std::ranges::any_of(diagnostics.diagnostics(), [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-WS-002"
            && diagnostic.message.find("deleted_provider") != std::string::npos;
    }));
    diagnostics.clear();

    publish_source(root, "alternate", "alternate", fsim::project::Language::system_verilog, "2017",
        "module leaf(output wire q, input wire a); assign q = a; endmodule\n");
    catalogs = detail::workspace_catalogs(store, nullptr, diagnostics);
    require(catalogs.has_value(), diagnostics);
    config = top_config(root, "sv:work.healthy");
    config.elaboration.search_libraries = { "alternate" };
    selection = detail::select_workspace_design(config, store, *catalogs, diagnostics);
    require(selection.has_value() && !diagnostics.has_error(), diagnostics);
    const auto names = selected_names(*selection);
    assert(names.contains("work.module.leaf") && names.contains("alternate.module.leaf"));
}

void check_vhdl_companions(const std::filesystem::path& root)
{
    publish_source(root, "vhdl-design", "design", fsim::project::Language::vhdl, "2008", R"(
package stored is
  constant value : integer;
end package;
package body stored is
  constant value : integer := 3;
end package body;
package unused_pending is
  function unfinished return integer;
end package;
entity vh_top is end;
use work.stored.all;
architecture rtl of vh_top is
begin
  process begin
    assert value = 3 report "deferred package completion" severity failure;
    wait;
  end process;
end;
use work.unused_pending.all;
architecture other of vh_top is
begin
end;
)");
    workspace::Store store(root);
    fsim::diagnostic::Engine diagnostics;
    const auto catalogs = detail::workspace_catalogs(store, nullptr, diagnostics);
    require(catalogs.has_value(), diagnostics);
    auto config = top_config(root, "vhdl:design.vh_top(rtl)");
    auto selection = detail::select_workspace_design(config, store, *catalogs, diagnostics);
    require(selection.has_value() && !diagnostics.has_error(), diagnostics);
    const auto names = selected_names(*selection);
    assert(names.contains("design.entity.vh_top") && names.contains("design.architecture.rtl"));
    assert(names.contains("design.package.stored") && !names.contains("design.package.unused_pending"));
    assert(!names.contains("design.architecture.other"));
    assert(selection->objects.size() == 4U);
    const auto built = detail::build_workspace_objects(config, selection->objects, selection->plugins, diagnostics);
    require(built.has_value() && !diagnostics.has_error(), diagnostics);

    config = top_config(root, "vhdl:design.vh_top");
    selection = detail::select_workspace_design(config, store, *catalogs, diagnostics);
    require(selection.has_value() && !diagnostics.has_error(), diagnostics);
    assert(selected_names(*selection).contains("design.architecture.other"));
}

} // namespace

int main()
{
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    TemporaryDirectory temporary {
        std::filesystem::temp_directory_path() / ("fsim-workspace-selection-" + unique)
    };
    check_hdl_closure(temporary.path / "hdl");
    check_vhdl_companions(temporary.path / "vhdl");
    return 0;
}
