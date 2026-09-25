// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/project/project.hpp"
#include "fsim/semantic/hierarchy_path.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

void print_diagnostics(const fsim::diagnostic::Engine& diagnostics)
{
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
}

void test_repeated_design_artifact_load_releases_path_owners()
{
    namespace app = fsim::app;
    namespace project = fsim::project;

    const auto suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
            / ("fsim-hierarchy-path-reload-" + suffix) };
    std::filesystem::create_directories(directory.path);

    const auto source = directory.path / "path_reload.sv";
    {
        std::ofstream output(source);
        output << R"(module path_reload_leaf;
  logic seen;
  initial seen = 1'b1;
endmodule

module path_reload_top;
  logic marker;
  path_reload_leaf first();
  path_reload_leaf second();
  initial marker = 1'b0;
endmodule
)";
        assert(output.good());
    }

    project::Config config;
    config.manifest_path = "<hierarchy-path-reload>";
    config.base_directory = directory.path;
    config.project.name = "hierarchy-path-reload";
    config.project.tops.push_back({ "sv:work.path_reload_top", "root" });
    config.project.time_resolution = "1ns";
    config.build.cache_path = directory.path / "cache";
    project::SourceSet source_set;
    source_set.language = project::Language::system_verilog;
    source_set.standard = "2017";
    source_set.library = "work";
    source_set.compilation_unit = "source-set";
    source_set.file_patterns = { source };
    source_set.files = { source };
    config.source_sets.push_back(std::move(source_set));

    const auto object = directory.path / "path_reload.fsimobj";
    const auto artifact = directory.path / "path_reload.fsimdesign";
    fsim::diagnostic::Engine diagnostics;
    const bool compiled = app::compile_artifact(config, object, diagnostics);
    if (!compiled || diagnostics.has_error()) {
        print_diagnostics(diagnostics);
    }
    assert(compiled && !diagnostics.has_error());

    config.source_sets.clear();
    const std::array objects { object };
    const bool elaborated = app::elaborate_artifact(
        config, objects, artifact, diagnostics);
    if (!elaborated || diagnostics.has_error()) {
        print_diagnostics(diagnostics);
    }
    assert(elaborated && !diagnostics.has_error());

    constexpr std::size_t load_count = 32U;
    std::optional<fsim::semantic::HierarchyPathTable> retained_paths;
    std::optional<fsim::semantic::HierarchyPathId> retained_leaf;
    std::string artifact_identity;
    for (std::size_t index = 0; index < load_count; ++index) {
        fsim::diagnostic::Engine load_diagnostics;
        {
            auto loaded = app::load_design_artifact(artifact, load_diagnostics);
            if (!loaded || load_diagnostics.has_error()) {
                print_diagnostics(load_diagnostics);
            }
            assert(loaded && !load_diagnostics.has_error());
            assert(loaded->design.top() == "root");
            assert(loaded->design_ir.top() == "root");
            assert(loaded->design.roots().size() == 1U);
            assert(loaded->design_ir.roots().size() == 1U);

            const auto& runtime_paths = loaded->design.hierarchy_paths();
            const auto& ir_paths = loaded->design_ir.hierarchy_paths();
            const auto root = ir_paths.find("root");
            const auto first = ir_paths.find("root.first.seen");
            const auto second = ir_paths.find("root.second.seen");
            assert(root && first && second);
            assert(loaded->design_ir.roots().front() == *root);
            assert(runtime_paths.find("root.first.seen") == first);
            assert(runtime_paths.find("root.second.seen") == second);
            assert(loaded->design.find_signal("root.first.seen"));
            assert(loaded->design.find_signal("root.second.seen"));

            if (index == 0U) {
                retained_paths = ir_paths;
                retained_leaf = first;
                artifact_identity = loaded->artifact_identity;
                assert(!artifact_identity.empty());
            } else {
                assert(loaded->artifact_identity == artifact_identity);
                assert(first == retained_leaf);
                assert(ir_paths.size() == retained_paths->size());
            }
        }

        // The loaded project's owner is gone. A detached immutable table must
        // keep its path views valid across all subsequent artifact reloads.
        assert(retained_paths);
        assert(retained_leaf);
        assert(retained_paths->view(*retained_leaf) == "root.first.seen");
    }
}

} // namespace

int main()
{
    test_repeated_design_artifact_load_releases_path_owners();
}
