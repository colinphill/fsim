// SPDX-License-Identifier: Apache-2.0
// Prints the names fsim's projected IEEE packages declare for each VHDL
// standard (`standard_package_declarations` of the compiler-supplied units), so
// that the derived per-standard package sources can be compared against them
// (cmake/RunIeeeNameAgreement.cmake). Usage: tool WORK_DIRECTORY OUTPUT_FILE.
#include "fsim/app/application.hpp"
#include "fsim/app/built_project.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/project/project.hpp"
#include "fsim/semantic/vhdl_hir.hpp"
#include "fsim/support/vhdl_identifier.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::array<std::string_view, 5> standards {
    "1987", "1993", "2002", "2008", "2019"
};
constexpr std::array<std::string_view, 13> packages {
    "std_logic_1164", "std_logic_textio", "numeric_bit", "numeric_std",
    "math_real", "math_complex", "numeric_bit_unsigned",
    "numeric_std_unsigned", "fixed_float_types", "fixed_generic_pkg",
    "fixed_pkg", "float_generic_pkg", "float_pkg"
};

} // namespace

int main(const int argc, const char* const argv[])
{
    if (argc != 3) {
        std::cerr << "usage: tool WORK_DIRECTORY OUTPUT_FILE\n";
        return 2;
    }
    const std::filesystem::path directory { argv[1] };
    std::filesystem::create_directories(directory);
    std::ofstream output { argv[2] };
    for (const auto standard : standards) {
        for (const auto package : packages) {
            const auto source = directory / "probe.vhd";
            {
                std::ofstream stream { source };
                stream << "library ieee;\nuse ieee." << package
                       << ".all;\nentity probe is\nend probe;\n";
            }
            fsim::project::Config config;
            config.base_directory = directory;
            config.project.name = "ieee-names";
            config.project.top = "vhdl:work.probe";
            config.project.time_resolution = "1ns";
            config.build.cache_path = directory / "cache";
            fsim::project::SourceSet sources;
            sources.language = fsim::project::Language::vhdl;
            sources.standard = std::string { standard };
            sources.library = "work";
            sources.files.push_back(source);
            config.source_sets.push_back(std::move(sources));
            fsim::diagnostic::Engine diagnostics;
            const auto checked = fsim::app::check_project(config, diagnostics);
            if (!checked) {
                std::string code = "none";
                for (const auto& diagnostic : diagnostics.diagnostics()) {
                    if (diagnostic.severity
                        == fsim::diagnostic::Severity::error) {
                        code = diagnostic.code;
                        break;
                    }
                }
                output << "UNAVAILABLE\t" << standard << '\t' << package
                       << '\t' << code << '\n';
                continue;
            }
            const auto& units = checked->vhdl_hir.units();
            const auto found = std::ranges::find_if(
                units, [&](const fsim::semantic::vhdl::Unit& unit) {
                    return unit.kind == fsim::semantic::vhdl::UnitKind::package
                        && unit.library == "ieee" && unit.name == package;
                });
            if (found == units.end()) {
                output << "UNAVAILABLE\t" << standard << '\t' << package
                       << "\tabsent\n";
                continue;
            }
            std::vector<std::string> names = found->standard_package_declarations;
            std::ranges::sort(names);
            names.erase(std::unique(names.begin(), names.end()), names.end());
            output << "NAMES\t" << standard << '\t' << package;
            for (const auto& name : names) {
                output << '\t' << name;
            }
            output << '\n';
            // Declarations the compiler-supplied unit holds as ordinary HIR
            // declarations (for instance the X01 subtypes of STD_LOGIC_1164).
            const auto& hir = checked->vhdl_hir;
            std::vector<std::string> declared;
            for (const auto id : found->declarations) {
                declared.push_back(fsim::support::ascii_lower(
                    hir.declarations()[id.value()].name));
            }
            std::ranges::sort(declared);
            declared.erase(
                std::unique(declared.begin(), declared.end()), declared.end());
            output << "DECLARED\t" << standard << '\t' << package;
            for (const auto& name : declared) {
                output << '\t' << name;
            }
            output << '\n';
        }
    }
    return output ? 0 : 1;
}
