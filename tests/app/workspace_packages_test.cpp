// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_internal.hpp"

#include "fsim/app/design_artifact.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void write_file(const std::filesystem::path& path, std::string_view contents)
{
    std::ofstream output(path, std::ios::binary);
    assert(output);
    output << contents;
    assert(output.good());
}

fsim::project::Config configuration(const std::filesystem::path& root,
    std::vector<std::filesystem::path> sources)
{
    fsim::project::Config config;
    config.base_directory = root;
    config.project.name = "workspace-packages";
    config.project.time_resolution = "1ns";
    config.build.cache_path = root / "cache";
    config.build.jobs = 12;
    fsim::project::SourceSet source_set;
    source_set.language = fsim::project::Language::system_verilog;
    source_set.standard = "2017";
    source_set.library = "work";
    source_set.compilation_unit = "source-set";
    source_set.include_directories = { root };
    source_set.files = std::move(sources);
    config.source_sets.push_back(std::move(source_set));
    return config;
}

fsim::app::application_detail::CompilationWorkspace compile(
    const fsim::project::Config& config,
    const fsim::semantic::CompiledDesign* imported = nullptr)
{
    fsim::diagnostic::Engine diagnostics;
    auto checked = fsim::app::application_detail::check_project_for_object(
        config, diagnostics, imported);
    if (!checked || diagnostics.has_error())
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    assert(checked && !diagnostics.has_error());
    return std::move(*checked);
}

void verify_source_ownership(const std::filesystem::path& root)
{
    const auto package = root / "package.sv";
    write_file(package, R"(
package stored_pkg;
  parameter int STORED_VALUE = 17;
endpackage
)");
    auto package_checked = compile(configuration(root, { package }));
    fsim::diagnostic::Engine diagnostics;
    const auto source_mappings
        = fsim::app::application_detail::compiled_cache_source_mappings(
            package_checked, root, diagnostics);
    assert(source_mappings && !diagnostics.has_error());
    assert(fsim::app::application_detail::relocate_compiled_design_sources(
        package_checked, *source_mappings, diagnostics));
    const auto bytes = fsim::app::serialize_compiled_hir_bundle(
        package_checked, diagnostics);
    assert(bytes && !diagnostics.has_error());
    package_checked = { };
    assert(std::filesystem::remove(package));
    auto imported = fsim::app::deserialize_compiled_hir_bundle(
        *bytes, "stored workspace package", diagnostics);
    assert(imported && !diagnostics.has_error());

    const auto include = root / "declarations.svh";
    const auto first = root / "first.sv";
    const auto second = root / "second.sv";
    write_file(include, R"(
`default_nettype none
module included_owner;
  import stored_pkg::*;
  localparam int VALUE = STORED_VALUE;
  logic [31:0] result;
  initial result = STORED_VALUE;
endmodule
`default_nettype wire
)");
    write_file(first, "`include \"declarations.svh\"\n");
    write_file(second, R"(
module second_owner;
  import stored_pkg::*;
  wire STORED_VALUE;
  assign STORED_VALUE = 1'b1;
endmodule
)");
    auto checked = compile(configuration(root, { first, second }), &*imported);
    assert(checked.source_units.size() == checked.source_unit_paths.size());
    assert(checked.source_class_identities.size()
        == checked.source_class_paths.size());
    assert(checked.source_udp_identities.size() == checked.source_udp_paths.size());
    std::size_t named_units { };
    for (std::size_t index = 0; index < checked.source_units.size(); ++index) {
        const auto id = checked.source_units[index];
        const auto& unit = checked.semantics.units()[id.value()];
        assert(unit.name != "stored_pkg");
        if (unit.name == "included_owner") {
            assert(checked.source_unit_paths[index] == first);
            ++named_units;
        } else if (unit.name == "second_owner") {
            assert(checked.source_unit_paths[index] == second);
            ++named_units;
        }
    }
    assert(named_units == 2);
    assert(std::ranges::any_of(checked.systemverilog_hir.units(),
        [](const auto& unit) { return unit.name == "stored_pkg"; }));

    auto fresh = fsim::semantic::extract_compiled_objects(checked,
        checked.source_units, checked.source_class_identities,
        checked.source_udp_identities);
    assert(fresh.ok());
    assert(std::ranges::none_of(fresh.design->systemverilog_hir.units(),
        [](const auto& unit) { return unit.name == "stored_pkg"; }));
    std::vector<fsim::semantic::CompiledDesign> inputs;
    inputs.push_back(std::move(*imported));
    inputs.push_back(std::move(*fresh.design));
    auto linked = fsim::semantic::link_compiled_designs(std::move(inputs));
    assert(linked.ok());
    assert(std::ranges::count_if(linked.design->systemverilog_hir.units(),
        [](const auto& unit) { return unit.name == "stored_pkg"; }) == 1);
    const auto shadows = std::ranges::count_if(
        linked.design->systemverilog_hir.declarations(), [&](const auto& declaration) {
            return declaration.name == "STORED_VALUE"
                && declaration.form == fsim::semantic::sv::DeclarationForm::net;
        });
    assert(shadows == 1);
    const fsim::elaboration::Root simulation_root { "sv:work.included_owner", "dut" };
    const auto elaborated = fsim::elaboration::elaborate(*linked.design,
        std::span { &simulation_root, 1U }, { }, { }, nullptr, { });
    assert(elaborated.ok() && elaborated.design);
    const auto signal = elaborated.design->find_signal("dut.result");
    assert(signal);
    auto interpreter = elaborated.design->create_interpreter();
    interpreter->start();
    assert(interpreter->run().status == fsim::runtime::RunStatus::completed);
    assert(interpreter->signal_value(*signal).to_msb_string()
        == std::string(27U, '0') + "10001");
}

void verify_duplicate_environment_rejected(const std::filesystem::path& root)
{
    const auto original = root / "original.sv";
    const auto duplicate = root / "duplicate.sv";
    write_file(original, "package duplicate_pkg; endpackage\n");
    auto imported = compile(configuration(root, { original }));
    write_file(duplicate, "package duplicate_pkg; endpackage\n");
    fsim::diagnostic::Engine diagnostics;
    const auto checked = fsim::app::application_detail::check_project_for_object(
        configuration(root, { duplicate }), diagnostics, &imported);
    assert(!checked && diagnostics.has_error());
    assert(std::ranges::any_of(diagnostics.diagnostics(), [](const auto& item) {
        return item.message.find("collision") != std::string::npos;
    }));
}

void verify_auxiliary_ownership(const std::filesystem::path& root)
{
    const auto include = root / "classes.svh";
    const auto first = root / "first_class.sv";
    const auto second = root / "second_class.sv";
    write_file(include, R"(
class FirstClass;
  int value;
endclass
primitive source_udp(output value, input source);
  table
    0 : 0;
    1 : 1;
  endtable
endprimitive
)");
    write_file(first, "`include \"classes.svh\"\n");
    write_file(second, "class SecondClass; int value; endclass\n");
    const auto checked = compile(configuration(root, { first, second }));
    assert(checked.source_class_identities.size() == 2);
    for (std::size_t index = 0; index < checked.source_class_identities.size(); ++index) {
        const auto& identity = checked.source_class_identities[index];
        if (identity.ends_with("::FirstClass"))
            assert(checked.source_class_paths[index] == first);
        else {
            assert(identity.ends_with("::SecondClass"));
            assert(checked.source_class_paths[index] == second);
        }
    }
    assert(checked.source_udp_identities.size() == 1);
    assert(checked.source_udp_identities.front().name == "source_udp");
    assert(checked.source_udp_paths.front() == first);
}

} // namespace

int main()
{
    TemporaryDirectory directory { std::filesystem::temp_directory_path()
        / ("fsim-workspace-packages-"
            + std::to_string(std::chrono::steady_clock::now()
                                 .time_since_epoch().count())) };
    assert(std::filesystem::create_directory(directory.path));
    verify_source_ownership(directory.path);
    verify_duplicate_environment_rejected(directory.path);
    verify_auxiliary_ownership(directory.path);
}
