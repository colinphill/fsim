// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/frontend/parser.hpp"
#include "fsim/runtime/vcd_writer.hpp"
#include "fsim/support/environment.hpp"
#include "fsim/version.hpp"

#include "governed_process_limits.hpp"
#include "vhdl_ieee_integration_support.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;
    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::vector<std::filesystem::path>& sources,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "ieee_integration";
    config.project.top = "vhdl:work.ieee_integration(rtl)";
    config.project.time_resolution = "1ns";
    config.build.jobs = 8;
    config.build.optimization = optimization;
    config.build.cache_path = directory / (optimization == fsim::project::Optimization::o0 ? "cache-o0" : "cache-o2");
    config.run.max_deltas = 1000;
    for (const auto& source : sources) {
        fsim::project::SourceSet source_set;
        source_set.language = fsim::project::Language::vhdl;
        source_set.standard = "2008";
        source_set.library = "work";
        source_set.compilation_unit = "file";
        source_set.files.push_back(source);
        config.source_sets.push_back(std::move(source_set));
    }
    return config;
}

struct Capture {
    fsim::runtime::RunResult result;
    std::vector<std::string> values;
    std::size_t compiled_processes { };
    std::size_t compiled_modules { };
    fsim::app::NativeCacheStatistics native_cache;
    std::vector<std::string> local_values;
    std::string vcd;
};

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine,
    const std::vector<std::string>& names)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(project);
    const auto specialization = std::ranges::find_if(
        project->design.specializations(), [](const auto& candidate) {
            return candidate.instance == "ieee_integration";
        });
    assert(specialization != project->design.specializations().end());
    const auto delays = std::ranges::find_if(
        specialization->parameter_identity_values,
        [](const auto& value) {
            return value.first == "vital_delays_generic";
        });
    const auto map = std::ranges::find_if(
        specialization->parameter_identity_values,
        [](const auto& value) {
            return value.first == "vital_map_generic";
        });
    assert(delays != specialization->parameter_identity_values.end());
    assert(map != specialization->parameter_identity_values.end());
    assert(delays->second.find("vhdlcomposite-v1") != std::string::npos);
    assert(delays->second.find("width=128") != std::string::npos);
    assert(delays->second.ends_with(
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000010"));
    assert(map->second.find("vhdlcomposite-v1") != std::string::npos);
    assert(map->second.find("value=UX01") != std::string::npos);
    for (const std::string_view dependency : {
             "numeric_bit.vhdl", "numeric_std.vhdl", "fixed_pkg.vhdl",
             "float_pkg.vhdl", "vital_timing.vhdl",
             "vital_primitives.vhdl", "vital_memory.vhdl" }) {
        assert(std::ranges::any_of(
            specialization->source_dependencies,
            [&](const std::string& source) {
                return source.ends_with(dependency);
            }));
    }
    std::vector<std::pair<
        fsim::runtime::simir::ProcessId, std::size_t>>
        debug_locals;
    for (const auto& process : project->design.processes()) {
        for (std::size_t index = 0; index < process.debug_locals.size(); ++index) {
            if (process.debug_locals[index].name.starts_with("package_local_")) {
                debug_locals.emplace_back(process.id, index);
            }
        }
    }
    assert(debug_locals.size() == 4);
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    Capture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    capture.compiled_modules = simulation.compiled_module_count();
    capture.native_cache = simulation.native_cache_statistics();
    std::vector<fsim::runtime::simir::SignalId> signals;
    std::vector<fsim::runtime::VcdSignal> traces;
    std::ostringstream vcd_output;
    fsim::runtime::VcdWriter vcd { vcd_output, "1ns", 64 };
    for (const auto& name : names) {
        const auto signal = simulation.find_signal(name);
        assert(signal);
        signals.push_back(*signal);
        traces.push_back(vcd.declare_signal(
            name, simulation.read_signal(*signal).width()));
    }
    vcd.begin();
    for (std::size_t index = 0; index < signals.size(); ++index) {
        vcd.change(traces[index], simulation.read_signal(signals[index]));
    }
    simulation.set_signal_change_hook(
        [&](const fsim::runtime::simir::SignalId signal,
            const fsim::runtime::PackedLogic4& value,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t) {
            const auto found = std::ranges::find(signals, signal);
            if (found == signals.end()) {
                return;
            }
            const auto index = static_cast<std::size_t>(
                std::distance(signals.begin(), found));
            vcd.set_time(time);
            vcd.change(traces[index], value);
        });
    capture.result = simulation.run();
    for (const auto signal : signals) {
        capture.values.push_back(
            simulation.read_signal(signal).to_msb_string());
    }
    for (const auto& [process, local] : debug_locals) {
        capture.local_values.push_back(
            simulation.read_process_local(process, local).to_msb_string());
    }
    std::ranges::sort(capture.local_values);
    vcd.flush();
    capture.vcd = vcd_output.str();
    return capture;
}

void verify_analysis(const fsim::project::Config& config)
{
    fsim::diagnostic::Engine diagnostics;
    const auto checked = fsim::app::check_project(config, diagnostics);
    if (!checked) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(checked);
    assert(!diagnostics.has_error());
    assert(checked->source_count == 3);
    assert(checked->hdl_sources.size() == 3);
    assert(checked->standard_sources.size() == 19);
    for (const auto& source : checked->standard_sources) {
        assert(source.path.generic_string().starts_with("fsim-standard/ieee/"));
        if (source.path.filename() == "vital_timing.vhdl"
            || source.path.filename() == "vital_primitives.vhdl"
            || source.path.filename() == "vital_memory.vhdl") {
            assert(source.backing_path.empty());
        } else {
            assert(source.backing_path.generic_string().find(
                       "third_party/ieee-1076-2019/ieee/")
                != std::string::npos);
        }
    }
    std::vector<std::string> packages;
    for (const auto& unit : checked->parsed.units) {
        if (unit.kind == fsim::frontend::UnitKind::VhdlPackage
            && unit.library == "ieee" && unit.primary_name.empty()) {
            packages.push_back(unit.name);
        }
    }
    assert((packages == std::vector<std::string> { "std_logic_1164", "std_logic_textio", "numeric_bit", "numeric_std", "math_real", "fixed_float_types", "fixed_generic_pkg", "fixed_pkg", "float_generic_pkg", "float_pkg", "vital_timing", "vital_primitives", "vital_memory" }));
    for (const std::string_view package : {
             "std_logic_1164", "numeric_bit", "numeric_std", "math_real",
             "fixed_generic_pkg", "float_generic_pkg" }) {
        const auto declaration = std::ranges::find_if(
            checked->parsed.units,
            [&](const fsim::frontend::DesignUnit& unit) {
                return unit.kind == fsim::frontend::UnitKind::VhdlPackage
                    && unit.library == "ieee" && unit.name == package
                    && unit.primary_name.empty();
            });
        assert(declaration != checked->parsed.units.end());
        const auto body = std::next(declaration);
        assert(body != checked->parsed.units.end());
        assert(body->kind == fsim::frontend::UnitKind::VhdlPackage);
        assert(body->library == "ieee" && body->name == package);
        assert(body->primary_name == package);
    }
    const auto architecture = std::ranges::find_if(
        checked->parsed.units,
        [](const fsim::frontend::DesignUnit& unit) {
            return unit.kind == fsim::frontend::UnitKind::VhdlArchitecture
                && unit.name == "rtl";
        });
    assert(architecture != checked->parsed.units.end());
    const auto signal_type = [&](const std::string_view name) {
        const auto signal = std::ranges::find_if(
            architecture->signals,
            [&](const fsim::frontend::SignalDeclaration& candidate) {
                return candidate.name == name;
            });
        assert(signal != architecture->signals.end());
        return signal->type;
    };
    assert(signal_type("numeric_logic").domain
        == fsim::frontend::ValueDomain::Logic9);
    assert(signal_type("numeric_bits").domain
        == fsim::frontend::ValueDomain::Bit2);
    assert(signal_type("fixed_rounded").spelling == "ufixed");
    assert(signal_type("float_sum").spelling == "float");
    assert(signal_type("vital_transition").spelling
        == "vitaltransitiontype");
    assert(signal_type("vital_delays").spelling == "vitaldelaytype01");
    assert(signal_type("vital_map").spelling == "vitalresultmaptype");
    assert(signal_type("vital_table").spelling == "vitaltruthtabletype");
    const auto package_type = [&](const std::string_view package,
                                  const std::string_view name) {
        const auto declaration = std::ranges::find_if(
            checked->parsed.units,
            [&](const fsim::frontend::DesignUnit& unit) {
                return unit.kind == fsim::frontend::UnitKind::VhdlPackage
                    && unit.library == "ieee" && unit.name == package
                    && unit.primary_name.empty();
            });
        assert(declaration != checked->parsed.units.end());
        const auto alias = std::ranges::find_if(
            declaration->type_aliases,
            [&](const fsim::frontend::TypeAliasDeclaration& candidate) {
                return candidate.name == name;
            });
        assert(alias != declaration->type_aliases.end());
        return alias->type;
    };
    const auto transition = package_type(
        "vital_timing", "vitaltransitiontype");
    assert(transition.enumeration_literals.size() == 12);
    assert(transition.width() == 4);
    const auto delays = package_type("vital_timing", "vitaldelaytype01");
    assert(delays.width() == 128);
    assert(delays.vhdl_array);
    assert(delays.vhdl_array->dimensions.size() == 1);
    assert(delays.vhdl_array->dimensions.front().range);
    assert(delays.vhdl_array->dimensions.front().range->left == 0);
    assert(delays.vhdl_array->dimensions.front().range->right == 1);
    const auto map = package_type("vital_timing", "vitalresultmaptype");
    assert(map.width() == 4);
    const auto table = package_type(
        "vital_primitives", "vitaltruthtabletype");
    assert(!table.width());
    assert(table.vhdl_array);
    assert(table.vhdl_array->dimensions.size() == 2);
    const auto time_array = package_type("vital_timing", "vitaltimearrayt");
    assert(time_array.vhdl_array && !time_array.width());
    const auto time_access = package_type(
        "vital_timing", "vitaltimearraypt");
    assert(time_access.vhdl_access && time_access.width() == 32);
    const auto logic_access = package_type(
        "vital_timing", "vitallogicarraypt");
    assert(logic_access.vhdl_access && logic_access.width() == 32);
    const auto timing_data = package_type(
        "vital_timing", "vitaltimingdatatype");
    assert(timing_data.packed_members.size() == 11);
    assert(timing_data.packed_members.front().name == "notfirstflag");
    assert(timing_data.packed_members.back().name == "setupena");
    assert(timing_data.width() == 261);
    const auto period_data = package_type(
        "vital_timing", "vitalperioddatatype");
    assert(period_data.packed_members.size() == 4);
    assert(period_data.width() == 130);
    const auto glitch_kind = package_type(
        "vital_timing", "vitalglitchkindtype");
    const std::vector<std::string> glitch_literals {
        "onevent", "ondetect", "vitalinertial", "vitaltransport"
    };
    assert(glitch_kind.enumeration_literals == glitch_literals);
    const auto glitch_data = package_type(
        "vital_timing", "vitalglitchdatatype");
    assert(glitch_data.packed_members.size() == 4);
    assert(glitch_data.width() == 130);
    for (const auto& [record_name, array_name, width] : {
             std::tuple {
                 "vitalpathtype", "vitalpatharraytype", std::uint64_t { 129 } },
             std::tuple {
                 "vitalpath01type", "vitalpatharray01type",
                 std::uint64_t { 193 } },
             std::tuple {
                 "vitalpath01ztype", "vitalpatharray01ztype",
                 std::uint64_t { 449 } } }) {
        const auto record = package_type("vital_timing", record_name);
        assert(record.packed_members.size() == 3U);
        assert(record.packed_members[0].name == "inputchangetime");
        assert(record.packed_members[1].name == "pathdelay");
        assert(record.packed_members[2].name == "pathcondition");
        assert(record.width() == width);
        const auto array = package_type("vital_timing", array_name);
        assert(array.vhdl_array && !array.width());
        assert(array.vhdl_array->element_types.size() == 1U);
        assert(array.vhdl_array->element_types.front().width() == width);
    }
    const auto skew_data = package_type(
        "vital_timing", "vitalskewdatatype");
    assert(skew_data.packed_members.size() == 5);
    assert(skew_data.width() == 259);
    const auto memory_arc = package_type(
        "vital_memory", "vitalmemoryarctype");
    assert((memory_arc.enumeration_literals == std::vector<std::string> { "parallelarc", "crossarc", "subwordarc" }));
    const auto memory_schedule = package_type(
        "vital_memory", "vitalmemoryscheduledatatype");
    assert(memory_schedule.packed_members.size() == 8);
    assert(memory_schedule.packed_members[1].name == "numbitspersubword");
    assert(memory_schedule.width() == 291);
    const auto memory_timing = package_type(
        "vital_memory", "vitalmemorytimingdatatype");
    assert(memory_timing.packed_members.size() == 13);
    assert(memory_timing.packed_members[9].name == "reflasta");
    assert(memory_timing.width() == 325);
    const auto port_state = package_type(
        "vital_memory", "vitalportstatetype");
    assert(port_state.enumeration_literals.size() == 5);
    const auto port_flag = package_type(
        "vital_memory", "vitalportflagtype");
    assert(port_flag.packed_members.size() == 5);
    assert(port_flag.width() == 13);
    const auto memory_word_ptr = package_type(
        "vital_memory", "memorywordptr");
    assert(memory_word_ptr.vhdl_access && memory_word_ptr.width() == 32);
    const auto memory_data = package_type(
        "vital_memory", "vitalmemorydatatype");
    assert(memory_data.vhdl_access && memory_data.width() == 32);
    assert(memory_data.vhdl_access->designated_types.size() == 1);
    assert(memory_data.vhdl_access->designated_types.front().width() == 160);
    const auto memory_symbol = package_type(
        "vital_memory", "vitalmemorysymboltype");
    assert(memory_symbol.enumeration_literals.size() == 40);
    const auto memory_table = package_type(
        "vital_memory", "vitalmemorytabletype");
    assert(memory_table.vhdl_array);
    assert(memory_table.vhdl_array->dimensions.size() == 2);
    const auto violation_table = package_type(
        "vital_memory", "vitalmemoryviolationtabletype");
    assert(violation_table.vhdl_array);
    assert(violation_table.vhdl_array->dimensions.size() == 2);
    const auto address_vector = package_type(
        "vital_memory", "vitaladdressvaluevectortype");
    assert(address_vector.vhdl_array && !address_vector.width());
}

void verify_revision_environments(const std::filesystem::path& directory)
{
    struct Expected {
        std::string_view year;
        std::size_t declarations;
        std::size_t operators;
        std::size_t attributes;
        std::size_t logic_operators;
    };
    for (const auto& expected : std::array {
             Expected { "1987", 13, 28, 15, 8 },
             Expected { "1993", 15, 36, 30, 14 },
             Expected { "2000", 15, 36, 30, 14 },
             Expected { "2002", 15, 36, 30, 14 },
             Expected { "2008", 19, 45, 32, 17 },
             Expected { "2019", 19, 45, 34, 17 } }) {
        const auto source = directory / ("environment-" + std::string { expected.year } + ".vhd");
        const auto unit_name = "environment_" + std::string { expected.year };
        {
            std::ofstream output { source };
            output << "library ieee;\nuse ieee.std_logic_1164.all;\n";
            if (expected.year != "1987") {
                output << "use ieee.numeric_std.all;\n";
            }
            output << "use ieee.std_logic_arith.all;\n"
                      "use ieee.std_logic_signed.all;\n"
                      "use ieee.std_logic_unsigned.all;\n"
                      "use ieee.std_logic_misc.all;\n"
                      "entity "
                   << unit_name << " is end " << unit_name << ";\n"
                   << "architecture rtl of " << unit_name
                   << " is\n  signal source : std_logic_vector(136 downto 0);\n"
                      "  signal result : std_logic_vector(136 downto 0);\n"
                      "begin\n  result <= source;\nend rtl;\n";
            assert(output.good());
        }
        fsim::project::Config config;
        config.base_directory = directory;
        config.project.name = unit_name;
        config.project.top = "vhdl:legacy_work." + unit_name + "(rtl)";
        config.project.time_resolution = "1fs";
        config.build.jobs = 8;
        fsim::project::SourceSet source_set;
        source_set.language = fsim::project::Language::vhdl;
        source_set.standard = expected.year;
        source_set.library = "legacy_work";
        source_set.compilation_unit = "file";
        source_set.files.push_back(source);
        config.source_sets.push_back(std::move(source_set));

        fsim::diagnostic::Engine diagnostics;
        const auto checked = fsim::app::check_project(config, diagnostics);
        if (!checked) {
            for (const auto& diagnostic : diagnostics.diagnostics()) {
                std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
            }
        }
        assert(checked && !diagnostics.has_error());
        const auto architecture = std::ranges::find_if(
            checked->parsed.units,
            [&](const fsim::frontend::DesignUnit& unit) {
                return unit.kind == fsim::frontend::UnitKind::VhdlArchitecture
                    && unit.name == "rtl" && unit.primary_name == unit_name;
            });
        assert(architecture != checked->parsed.units.end());
        const auto& environment = architecture->vhdl_predefined_environment;
        assert(environment.identity
            == "ieee-1076-standard:" + std::string { expected.year }
                + ":fsim-v3");
        assert(environment.working_library == "legacy_work");
        assert((environment.implicit_libraries
            == std::vector<std::string> { "std", "work" }));
        assert((environment.implicit_packages
            == std::vector<std::string> { "std.standard.all" }));
        assert(environment.declarations.size() == expected.declarations);
        assert(environment.operator_profiles.size() == expected.operators);
        if (environment.attributes.size() != expected.attributes) {
            std::cerr << "VHDL-" << expected.year << " attributes: observed "
                      << environment.attributes.size() << ", expected "
                      << expected.attributes << '\n';
        }
        assert(environment.attributes.size() == expected.attributes);
        assert((environment.time_units == std::vector<std::string> { "fs", "ps", "ns", "us", "ms", "sec", "min", "hr" }));
        assert(environment.default_time_unit == "fs");

        const auto logic_package = std::ranges::find_if(
            checked->parsed.units,
            [](const fsim::frontend::DesignUnit& unit) {
                return unit.kind == fsim::frontend::UnitKind::VhdlPackage
                    && unit.library == "ieee" && unit.name == "std_logic_1164"
                    && unit.primary_name.empty();
            });
        assert(logic_package != checked->parsed.units.end());
        assert(logic_package->vhdl_standard == architecture->vhdl_standard);
        assert(logic_package->standard_package_operator_profiles.size()
            == expected.logic_operators);
        const auto has_export = [&](const std::string_view name) {
            return std::ranges::find(
                       logic_package->standard_package_declarations, name)
                != logic_package->standard_package_declarations.end();
        };
        assert(has_export("std_logic_vector"));
        assert(has_export("xnor") == (expected.year != "1987"));
        assert(has_export("to_hstring")
            == (expected.year == "2008" || expected.year == "2019"));

        const auto package_unit = [&](const std::string_view name) {
            return std::ranges::find_if(
                checked->parsed.units,
                [&](const fsim::frontend::DesignUnit& unit) {
                    return unit.kind == fsim::frontend::UnitKind::VhdlPackage
                        && unit.library == "ieee" && unit.name == name
                        && unit.primary_name.empty();
                });
        };
        const auto has_package_item = [](const auto& package,
                                          const std::string_view name) {
            return std::ranges::find(
                       package->standard_package_declarations, name)
                != package->standard_package_declarations.end();
        };
        const auto has_package_profile = [](const auto& package,
                                             const std::string_view profile) {
            return std::ranges::find(
                       package->standard_package_operator_profiles, profile)
                != package->standard_package_operator_profiles.end();
        };
        const auto arith = package_unit("std_logic_arith");
        const auto signed_package = package_unit("std_logic_signed");
        const auto unsigned_package = package_unit("std_logic_unsigned");
        const auto misc = package_unit("std_logic_misc");
        assert(arith != checked->parsed.units.end());
        assert(signed_package != checked->parsed.units.end());
        assert(unsigned_package != checked->parsed.units.end());
        assert(misc != checked->parsed.units.end());
        const auto numeric_package = package_unit("numeric_std");
        assert((numeric_package != checked->parsed.units.end())
            == (expected.year != "1987"));
        for (const auto package : { arith, signed_package, unsigned_package, misc }) {
            assert(package->vhdl_standard == architecture->vhdl_standard);
            assert(package->standard_package_revision.starts_with(
                "synopsys-legacy-ieee:1990-1992:"
                "fsim-synopsys-ieee-compat-v2:"));
            assert(package->standard_package_revision.ends_with(
                ":vhdl-" + std::string { expected.year }));
        }
        assert(arith < signed_package && signed_package < unsigned_package
            && unsigned_package < misc);
        assert(has_package_item(arith, "unsigned"));
        assert(has_package_item(arith, "small_int"));
        assert(has_package_item(arith, "conv_std_logic_vector"));
        assert(has_package_item(arith, "ext"));
        assert(has_package_item(arith, "sxt"));
        assert(has_package_profile(
            arith, "+(unsigned,signed)->signed"));
        assert(has_package_profile(
            arith, "+(unsigned,signed)->std_logic_vector"));
        assert(has_package_profile(
            arith, "conv_signed(std_ulogic,integer)->signed"));
        assert(has_package_item(signed_package, "abs"));
        assert(has_package_profile(
            signed_package, "-(std_logic_vector)->std_logic_vector"));
        assert(has_package_profile(
            signed_package,
            "<(std_logic_vector,integer)->boolean"));
        assert(!has_package_item(unsigned_package, "abs"));
        assert(has_package_profile(
            unsigned_package,
            "*(std_logic_vector,std_logic_vector)->std_logic_vector"));
        assert(has_package_item(misc, "strength"));
        assert(has_package_item(misc, "xnor_reduce"));
        assert(has_package_profile(
            misc, "xnor_reduce(std_ulogic_vector)->ux01"));
        for (const auto package_name : { std::string_view { "std_logic_arith" },
                 std::string_view { "std_logic_signed" },
                 std::string_view { "std_logic_unsigned" },
                 std::string_view { "std_logic_misc" } }) {
            const auto dependency_path = std::filesystem::path { "fsim-standard" }
                / "ieee" / "synopsys"
                / (std::string { package_name } + ".vhdl");
            const auto dependency = std::ranges::find_if(
                checked->standard_sources,
                [&](const auto& candidate) {
                    return candidate.path == dependency_path;
                });
            assert(dependency != checked->standard_sources.end());
            assert(dependency->backing_path.empty());
            assert(!dependency->content_digest.empty());
        }
    }

    const auto unavailable_source = directory / "environment-unavailable.vhd";
    {
        std::ofstream output { unavailable_source };
        output << R"(
library ieee;
use ieee.std_logic_1164.all;
entity environment_unavailable is end environment_unavailable;
architecture rtl of environment_unavailable is
begin
  observe : process
  begin
    report to_hstring(bit_vector'("10"));
    wait;
  end process;
end rtl;
)";
        assert(output.good());
    }
    fsim::project::Config unavailable_config;
    unavailable_config.base_directory = directory;
    unavailable_config.project.name = "environment_unavailable";
    unavailable_config.project.top = "vhdl:work.environment_unavailable(rtl)";
    unavailable_config.project.time_resolution = "1fs";
    fsim::project::SourceSet unavailable_set;
    unavailable_set.language = fsim::project::Language::vhdl;
    unavailable_set.standard = "1993";
    unavailable_set.library = "work";
    unavailable_set.compilation_unit = "file";
    unavailable_set.files.push_back(unavailable_source);
    unavailable_config.source_sets.push_back(std::move(unavailable_set));
    fsim::diagnostic::Engine unavailable_diagnostics;
    const auto unavailable = fsim::app::build_project(
        unavailable_config, unavailable_diagnostics);
    assert(!unavailable);
    assert(std::ranges::any_of(
        unavailable_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-VHSTD-001";
        }));

    const auto conflict_source = directory / "synopsys-conflict.vhd";
    {
        std::ofstream output { conflict_source };
        output << R"(library ieee;
use ieee.std_logic_signed.all;
package std_logic_signed is
end package std_logic_signed;
)";
        assert(output.good());
    }
    fsim::project::Config conflict_config;
    conflict_config.base_directory = directory;
    conflict_config.project.name = "synopsys_conflict";
    fsim::project::SourceSet conflict_set;
    conflict_set.language = fsim::project::Language::vhdl;
    conflict_set.standard = "1993";
    conflict_set.library = "ieee";
    conflict_set.compilation_unit = "file";
    conflict_set.files.push_back(conflict_source);
    conflict_config.source_sets.push_back(std::move(conflict_set));
    fsim::diagnostic::Engine conflict_diagnostics;
    const auto conflict = fsim::app::check_project(conflict_config, conflict_diagnostics);
    assert(!conflict);
    assert(std::ranges::any_of(
        conflict_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-FE-VHSTD-004"
                && diagnostic.message.find("non-standard Synopsys")
                != std::string::npos
                && diagnostic.message.find("another logical library")
                != std::string::npos;
        }));

    fsim::project::Config mixed_config;
    mixed_config.base_directory = directory;
    mixed_config.project.name = "mixed_revision_ieee";
    for (const auto& [year, library] : std::array {
             std::pair<std::string_view, std::string_view> { "1987", "legacy87" },
             std::pair<std::string_view, std::string_view> { "1993", "legacy93" } }) {
        const auto source = directory / ("mixed-" + std::string { year } + ".vhd");
        const auto name = "mixed_" + std::string { year };
        {
            std::ofstream output { source };
            output << "library ieee;\nuse ieee.std_logic_unsigned.all;\nentity "
                   << name << " is end " << name << ";\n";
            assert(output.good());
        }
        fsim::project::SourceSet source_set;
        source_set.language = fsim::project::Language::vhdl;
        source_set.standard = year;
        source_set.library = library;
        source_set.compilation_unit = "file";
        source_set.files.push_back(source);
        mixed_config.source_sets.push_back(std::move(source_set));
    }
    fsim::diagnostic::Engine mixed_diagnostics;
    const auto mixed = fsim::app::check_project(mixed_config, mixed_diagnostics);
    assert(!mixed);
    assert(std::ranges::any_of(
        mixed_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-FE-VHSTD-005";
        }));
}

} // namespace

int main()
{
    fsim::test::install_governed_process_address_space_ceiling();
    const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-vhdl-ieee-integration-" + std::to_string(serial))
    };
    std::filesystem::create_directories(directory.path);
    verify_revision_environments(directory.path);
    verify_vhdl2019_governed_packages(directory.path);
    verify_vhdl2019_simulator_api(directory.path);
    verify_vhdl2019_directory_api(directory.path);
    verify_vhdl2019_environment_api(directory.path);
    verify_vhdl2019_assert_api(directory.path);

    const auto context = directory.path / "00_context.vhd";
    const auto entity = directory.path / "01_entity.vhd";
    const auto architecture = directory.path / "02_architecture.vhd";
    {
        // FSIM-CONFORMANCE CF-VHDL-PACKAGE-001 source=SRC-IEEE-P1076 expectation=execute
        std::ofstream output { context };
        output << R"(
context ieee_all is
  library ieee;
  use ieee.std_logic_1164.all;
  use ieee.std_logic_textio.all;
  use ieee.numeric_bit.all;
  use ieee.numeric_std.all;
  use ieee.fixed_pkg.all;
  use ieee.float_pkg.all;
  use ieee.vital_timing.all;
  use ieee.vital_primitives.all;
  use ieee.vital_memory.all;
end context ieee_all;
)";
        assert(output.good());
    }
    {
        std::ofstream output { entity };
        output << R"(
context work.ieee_all;
entity ieee_integration is
  generic (
    vital_delays_generic : VitalDelayType01 := (1 ns, 2 ns);
    vital_map_generic : VitalResultMapType := VitalDefaultResultMap);
end entity;
)";
        assert(output.good());
    }
    {
        std::ofstream output { architecture };
        output << R"(
context work.ieee_all;
architecture rtl of ieee_integration is
  signal mapped : std_logic_vector(7 downto 0);
  signal numeric_logic : ieee.numeric_std.unsigned(11 downto 0);
  signal numeric_bits : ieee.numeric_bit.unsigned(7 downto 0);
  signal fixed_source : ufixed(1 downto -4);
  signal fixed_rounded : ufixed(1 downto -2);
  signal float_sum : float(8 downto -23);
  signal vital_transition : VitalTransitionType;
  signal vital_delays : VitalDelayType01;
  signal vital_map : VitalResultMapType;
  signal vital_table : VitalTruthTableType(0 to 0, 0 to 1);
  signal vital_memory_arc : VitalMemoryArcType;
  signal vital_memory_symbol : VitalMemorySymbolType;
  signal vital_memory_table : VitalMemoryTableType(0 to 0, 0 to 1);
  signal vital_buf : std_logic;
  signal vital_inv : std_logic;
  signal vital_and : std_logic;
  signal vital_nand4 : std_logic;
  signal vital_custom_map : std_logic;
  signal vital_generic_map : std_logic;
  signal vital_ident : std_logic;
  signal vital_extended : VitalDelayType01Z;
  signal vital_delay_match : boolean;
  signal vital_bufif_enabled : std_logic;
  signal vital_bufif_disabled : std_logic;
  signal vital_invif_unknown : std_logic;
  signal vital_bufif_mapped : std_logic;
  signal vital_mux_unknown : std_logic;
  signal vital_mux_same : std_logic;
  signal vital_mux4 : std_logic;
  signal vital_decoder4 : std_logic_vector(3 downto 0);
  signal vital_decoder_disabled : std_logic_vector(1 downto 0);
  signal vital_truth_scalar : std_logic;
  signal vital_truth_first : std_logic;
  signal vital_truth_vector : std_logic_vector(1 downto 0);
  signal vital_ascending_data : std_logic_vector(0 to 3);
  signal vital_wide_data : std_logic_vector(0 to 64);
  signal vital_and_ascending : std_logic;
  signal vital_and_wide : std_logic;
  signal vital_and_singleton : std_logic;
  signal vital_and_null : std_logic;
  signal vital_or2 : std_logic;
  signal vital_xor3 : std_logic;
  signal vital_xnor4 : std_logic;
  signal vital_mux8 : std_logic;
  signal vital_decoder8 : std_logic_vector(7 downto 0);
  signal vital_ident_u : std_logic;
  signal vital_ident_dash : std_logic;
  signal vital_mux_ascending : std_logic;
  signal vital_clock : std_logic;
  signal vital_period_seen : std_logic;
  signal vital_state_input : std_logic_vector(0 downto 0);
  signal vital_state_variable_vector : std_logic_vector(1 downto 0);
  signal vital_state_variable_scalar : std_logic;
  signal vital_state_signal_vector : std_logic_vector(1 downto 0);
  signal vital_state_signal_scalar : std_logic;
  signal vital_state_input_ascending : std_logic_vector(0 to 0);
  signal vital_state_signal_ascending : std_logic_vector(0 to 1);
  signal vital_state_zero_states : std_logic_vector(1 downto 0);
  signal vital_state_null_input : std_logic;
  signal vital_state_no_match : std_logic;
  signal vital_state_z_output : std_logic;
  signal vital_timing_test : std_logic;
  signal vital_timing_vector : std_logic_vector(1 downto 0);
  signal vital_timing_reference : std_logic;
  signal vital_recovery_test : std_logic;
  signal vital_setup_scalar_seen : std_logic;
  signal vital_setup_vector_seen : std_logic;
  signal vital_recovery_seen : std_logic;
  signal vital_skew_signal1 : std_logic;
  signal vital_skew_signal2 : std_logic;
  signal vital_out_skew_signal2 : std_logic;
  signal vital_in_skew_trigger : std_logic;
  signal vital_out_skew_trigger : std_logic;
  signal vital_in_skew_seen : std_logic;
  signal vital_out_skew_seen : std_logic;
begin
  mapped <= to_x01("ULH-WZ01");
  numeric_logic <= ieee.numeric_std.resize(
      ieee.numeric_std.to_unsigned(3, 8), 12);
  numeric_bits <= ieee.numeric_bit.to_unsigned(5, 8);
  fixed_source <= "000110";
  fixed_rounded <= resize(fixed_source, 1, -2);
  float_sum <= add(to_float(1, 8, 23), to_float(2, 8, 23));
  vital_buf <= VitalBUF('H');
  vital_inv <= VitalINV(Data => 'L');
  vital_and <= VitalAND("11H1");
  vital_nand4 <= VitalNAND4('1', '1', 'H', '1');
  vital_custom_map <= VitalOR2(
      a => '0', b => '0', ResultMap => "10X0");
  vital_generic_map <= VitalAND("11H1", vital_map_generic);
  vital_ident <= VitalIDENT('W');
  vital_delays <= VitalZeroDelay01;
  vital_extended <= VitalExtendToFillDelay(vital_delays_generic);
  vital_delay_match <= VitalCalcDelay(
      'Z', '0', VitalExtendToFillDelay(vital_delays_generic)) = 1 ns;
  vital_bufif_enabled <= VitalBUFIF1('H', '1');
  vital_bufif_disabled <= VitalBUFIF0('1', '1');
  vital_invif_unknown <= VitalINVIF1('0', 'X');
  vital_bufif_mapped <= VitalBUFIF1('1', '0', "UX010");
  vital_mux_unknown <= VitalMUX2('1', '0', 'X');
  vital_mux_same <= VitalMUX2('H', '1', 'X');
  vital_mux4 <= VitalMUX4("1010", "01");
  vital_decoder4 <= VitalDECODER4("10", '1');
  vital_decoder_disabled <= VitalDECODER2('X', '0');
  vital_truth_scalar <= VitalTruthTable(
      (('0', '1'), ('1', '0')), "0");
  vital_truth_first <= VitalTruthTable(
      (('-', '1'), ('0', '0')), "0");
  vital_truth_vector <= VitalTruthTable(
      (('0', '1', 'Z'), ('1', '0', '1')), "1");
  vital_ascending_data <= "11H1";
  vital_wide_data <=
      "11111111111111111111111111111111111111111111111111111111111111111";
  vital_and_ascending <= VitalAND(vital_ascending_data);
  vital_and_wide <= VitalAND(vital_wide_data);
  vital_and_singleton <= VitalAND("H");
  vital_and_null <= VitalAND("");
  vital_or2 <= VitalOR2('0', 'H');
  vital_xor3 <= VitalXOR3('1', '0', '1');
  vital_xnor4 <= VitalXNOR4('1', '0', '1', '0');
  vital_mux8 <= VitalMUX8("01011010", "110");
  vital_decoder8 <= VitalDECODER8("101", '1');
  vital_ident_u <= VitalIDENT('U');
  vital_ident_dash <= VitalIDENT('-');
  vital_mux_ascending <= VitalMUX4(vital_ascending_data, "01");
  vital_stimulus : process
  begin
    vital_clock <= '0';
    vital_state_input <= "0";
    vital_state_input_ascending <= "0";
    vital_timing_test <= '0';
    vital_timing_vector <= "00";
    vital_timing_reference <= '0';
    vital_recovery_test <= '0';
    vital_skew_signal1 <= '0';
    vital_skew_signal2 <= '0';
    vital_out_skew_signal2 <= '1';
    wait for 4 ns;
    vital_timing_test <= '1';
    vital_timing_vector <= "11";
    vital_recovery_test <= '1';
    wait for 1 ns;
    vital_clock <= '1';
    vital_state_input <= "1";
    vital_state_input_ascending <= "1";
    vital_timing_reference <= '1';
    vital_skew_signal1 <= '1';
    wait for 1 ns;
    vital_timing_test <= '0';
    vital_timing_vector <= "00";
    vital_recovery_test <= '0';
    wait for 1 ns;
    vital_recovery_test <= '1';
    wait for 1 ns;
    vital_skew_signal2 <= '1';
    vital_out_skew_signal2 <= '0';
    wait for 2 ns;
    vital_clock <= '0';
    vital_state_input <= "0";
    vital_state_input_ascending <= "0";
    vital_timing_reference <= '0';
    wait for 10 ns;
    vital_clock <= '1';
    vital_state_input <= "1";
    vital_state_input_ascending <= "1";
    vital_timing_reference <= '1';
    wait;
  end process;
  vital_period_checker : process(vital_clock)
    variable timing_data : VitalPeriodDataType := VitalPeriodDataInit;
    variable violation_value : std_logic := '0';
  begin
    VitalPeriodPulseCheck(
        Violation => violation_value,
        PeriodData => timing_data,
        TestSignal => vital_clock,
        TestSignalName => "vital_clock",
        Period => 10 ns,
        PulseWidthHigh => 6 ns,
        PulseWidthLow => 6 ns,
        HeaderMsg => "fixture: ",
        XOn => TRUE,
        MsgOn => FALSE);
    if vital_clock = 'U' then
      vital_period_seen <= '0';
    elsif violation_value = 'X' then
      vital_period_seen <= '1';
    end if;
  end process;
  vital_timing_checker : process(
      vital_timing_test, vital_timing_vector,
      vital_timing_reference, vital_recovery_test)
    variable scalar_data : VitalTimingDataType := VitalTimingDataInit;
    variable vector_data : VitalTimingDataType := VitalTimingDataInit;
    variable recovery_data : VitalTimingDataType := VitalTimingDataInit;
    variable scalar_violation : std_logic := '0';
    variable vector_violation : std_logic := '0';
    variable recovery_violation : std_logic := '0';
  begin
    VitalSetupHoldCheck(
        Violation => scalar_violation,
        TimingData => scalar_data,
        TestSignal => vital_timing_test,
        TestSignalName => "scalar_data",
        RefSignal => vital_timing_reference,
        RefSignalName => "reference",
        SetupHigh => 2 ns,
        SetupLow => 2 ns,
        HoldHigh => 2 ns,
        HoldLow => 2 ns,
        RefTransition => '/');
    VitalSetupHoldCheck(
        Violation => vector_violation,
        TimingData => vector_data,
        TestSignal => vital_timing_vector,
        TestSignalName => "vector_data",
        TestDelay => 1 ns,
        RefSignal => vital_timing_reference,
        RefSignalName => "reference",
        SetupHigh => 2 ns,
        SetupLow => 2 ns,
        HoldHigh => 2 ns,
        HoldLow => 2 ns,
        RefTransition => '/',
        MsgOn => FALSE);
    VitalRecoveryRemovalCheck(
        Violation => recovery_violation,
        TimingData => recovery_data,
        TestSignal => vital_recovery_test,
        TestSignalName => "reset_n",
        RefSignal => vital_timing_reference,
        RefSignalName => "reference",
        Recovery => 2 ns,
        Removal => 2 ns,
        ActiveLow => TRUE,
        RefTransition => '/',
        MsgOn => FALSE);
    if vital_timing_reference = 'U' then
      vital_setup_scalar_seen <= '0';
      vital_setup_vector_seen <= '0';
      vital_recovery_seen <= '0';
    else
      if scalar_violation = 'X' then
        vital_setup_scalar_seen <= '1';
      end if;
      if vector_violation = 'X' then
        vital_setup_vector_seen <= '1';
      end if;
      if recovery_violation = 'X' then
        vital_recovery_seen <= '1';
      end if;
    end if;
  end process;
  vital_skew_checker : process(
      vital_skew_signal1, vital_skew_signal2, vital_out_skew_signal2,
      vital_in_skew_trigger, vital_out_skew_trigger)
    variable in_data : VitalSkewDataType := VitalSkewDataInit;
    variable out_data : VitalSkewDataType := VitalSkewDataInit;
    variable in_violation : std_logic := '0';
    variable out_violation : std_logic := '0';
  begin
    VitalInPhaseSkewCheck(
        Violation => in_violation,
        SkewData => in_data,
        Signal1 => vital_skew_signal1,
        Signal1Name => "signal1",
        Signal2 => vital_skew_signal2,
        Signal2Name => "signal2",
        SkewS1S2RiseRise => 2 ns,
        SkewS2S1RiseRise => 2 ns,
        SkewS1S2FallFall => 2 ns,
        SkewS2S1FallFall => 2 ns,
        MsgOn => FALSE,
        Trigger => vital_in_skew_trigger);
    VitalOutPhaseSkewCheck(
        Violation => out_violation,
        SkewData => out_data,
        Signal1 => vital_skew_signal1,
        Signal1Name => "signal1",
        Signal2 => vital_out_skew_signal2,
        Signal2Name => "signal2",
        SkewS1S2RiseFall => 2 ns,
        SkewS2S1RiseFall => 2 ns,
        SkewS1S2FallRise => 2 ns,
        SkewS2S1FallRise => 2 ns,
        MsgOn => FALSE,
        Trigger => vital_out_skew_trigger);
    if vital_skew_signal1 = 'U' then
      vital_in_skew_seen <= '0';
      vital_out_skew_seen <= '0';
    else
      if in_violation = 'X' then
        vital_in_skew_seen <= '1';
      end if;
      if out_violation = 'X' then
        vital_out_skew_seen <= '1';
      end if;
    end if;
  end process;
  vital_state_variable_checker : process(vital_state_input)
    variable vector_result : std_logic_vector(1 downto 0) := "00";
    variable scalar_result : std_logic := '0';
    variable vector_previous : std_logic_vector(0 downto 0) := "X";
    variable scalar_previous : std_logic_vector(0 downto 0) := "X";
    variable null_result : std_logic := '0';
    variable null_previous : std_logic_vector(0 downto 0) := "X";
  begin
    VitalStateTable(
        Result => vector_result,
        PreviousDataIn => vector_previous,
        StateTable => (
            ('/', '-', '1', '1'),
            ('\', '-', '0', '0'),
            ('-', '-', 'S', 'S')),
        DataIn => vital_state_input,
        NumStates => 1);
    VitalStateTable(
        Result => scalar_result,
        PreviousDataIn => scalar_previous,
        StateTable => (
            ('/', '-', '1'),
            ('\', '-', '0'),
            ('-', '-', 'S')),
        DataIn => vital_state_input);
    VitalStateTable(
        Result => null_result,
        PreviousDataIn => null_previous,
        StateTable => (
            ('0', '1'),
            ('1', '-')),
        DataIn => "");
    vital_state_variable_vector <= vector_result;
    vital_state_variable_scalar <= scalar_result;
    vital_state_null_input <= null_result;
  end process;
  vital_state_signal_vector_call : VitalStateTable(
      Result => vital_state_signal_vector,
      StateTable => (
          ('/', '-', '1', '1'),
          ('\', '-', '0', '0'),
          ('-', '-', 'S', 'S')),
      DataIn => vital_state_input,
      NumStates => 1);
  vital_state_signal_scalar_call : VitalStateTable(
      Result => vital_state_signal_scalar,
      StateTable => (
          ('/', '-', '1'),
          ('\', '-', '0'),
          ('-', '-', 'S')),
      DataIn => vital_state_input);
  vital_state_signal_ascending_call : VitalStateTable(
      Result => vital_state_signal_ascending,
      StateTable => (
          ('/', '-', '1', '0'),
          ('\', '-', '0', '1'),
          ('-', '-', 'S', 'S')),
      DataIn => vital_state_input_ascending,
      NumStates => 1);
  vital_state_zero_states_call : VitalStateTable(
      Result => vital_state_zero_states,
      StateTable => (
          ('/', '1', '0'),
          ('-', 'S', 'S')),
      DataIn => vital_state_input,
      NumStates => 0);
  vital_state_no_match_call : VitalStateTable(
      Result => vital_state_no_match,
      StateTable => (
          ('0', '-', '0'),
          ('0', '-', '1')),
      DataIn => vital_state_input);
  vital_state_z_output_call : VitalStateTable(
      Result => vital_state_z_output,
      StateTable => (
          ('/', '-', 'Z'),
          ('-', '-', '-')),
      DataIn => vital_state_input);
  package_debug : process
    variable package_local_numeric_logic : ieee.numeric_std.unsigned(7 downto 0)
        := ieee.numeric_std.to_unsigned(9, 8);
    variable package_local_numeric_bits : ieee.numeric_bit.unsigned(7 downto 0)
        := ieee.numeric_bit.to_unsigned(7, 8);
    variable package_local_fixed : ufixed(1 downto -4)
        := to_ufixed(2, 1, -4);
    variable package_local_float : float(8 downto -23)
        := to_float(2, 8, 23);
  begin
    wait;
  end process;
end architecture;
)";
        assert(output.good());
    }
    const std::vector<std::filesystem::path> sources {
        context, entity, architecture
    };
    const std::vector<std::string> names {
        "ieee_integration.mapped", "ieee_integration.numeric_logic",
        "ieee_integration.numeric_bits", "ieee_integration.fixed_source",
        "ieee_integration.fixed_rounded", "ieee_integration.float_sum",
        "ieee_integration.vital_buf", "ieee_integration.vital_inv",
        "ieee_integration.vital_and", "ieee_integration.vital_nand4",
        "ieee_integration.vital_custom_map",
        "ieee_integration.vital_generic_map",
        "ieee_integration.vital_ident",
        "ieee_integration.vital_extended",
        "ieee_integration.vital_delay_match",
        "ieee_integration.vital_bufif_enabled",
        "ieee_integration.vital_bufif_disabled",
        "ieee_integration.vital_invif_unknown",
        "ieee_integration.vital_bufif_mapped",
        "ieee_integration.vital_mux_unknown",
        "ieee_integration.vital_mux_same",
        "ieee_integration.vital_mux4",
        "ieee_integration.vital_decoder4",
        "ieee_integration.vital_decoder_disabled",
        "ieee_integration.vital_truth_scalar",
        "ieee_integration.vital_truth_first",
        "ieee_integration.vital_truth_vector",
        "ieee_integration.vital_and_ascending",
        "ieee_integration.vital_and_wide",
        "ieee_integration.vital_and_singleton",
        "ieee_integration.vital_and_null",
        "ieee_integration.vital_or2",
        "ieee_integration.vital_xor3",
        "ieee_integration.vital_xnor4",
        "ieee_integration.vital_mux8",
        "ieee_integration.vital_decoder8",
        "ieee_integration.vital_ident_u",
        "ieee_integration.vital_ident_dash",
        "ieee_integration.vital_mux_ascending",
        "ieee_integration.vital_period_seen",
        "ieee_integration.vital_state_variable_vector",
        "ieee_integration.vital_state_variable_scalar",
        "ieee_integration.vital_state_signal_vector",
        "ieee_integration.vital_state_signal_scalar",
        "ieee_integration.vital_state_signal_ascending",
        "ieee_integration.vital_state_zero_states",
        "ieee_integration.vital_state_null_input",
        "ieee_integration.vital_state_no_match",
        "ieee_integration.vital_state_z_output",
        "ieee_integration.vital_setup_scalar_seen",
        "ieee_integration.vital_setup_vector_seen",
        "ieee_integration.vital_recovery_seen",
        "ieee_integration.vital_in_skew_seen",
        "ieee_integration.vital_out_skew_seen"
    };
    const std::vector<std::string> expected {
        "X01XXX01", "000000000011", "00000101", "000110", "0010",
        "01000000010000000000000000000000", "1", "1", "1", "0",
        "X", "1", "W",
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000010"
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000010"
        "0000000000000000000000000000000000000000000000000000000000000010",
        "1", "1", "Z", "X", "0", "X", "1", "1", "0100", "00",
        "1", "1", "01", "1", "1", "1", "1", "1", "0", "1", "1",
        "00100000", "U", "-", "1", "1", "11", "1", "11", "1",
        "10", "10", "1", "X", "Z",
        "1", "1", "1", "1", "1"
    };
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto config = make_config(directory.path, sources, optimization);
        verify_analysis(config);
        const auto reference = run_once(
            config, fsim::app::SimulationEngine::interpreter, names);
        const auto compiled = run_once(
            config, fsim::app::SimulationEngine::compiled, names);
        const auto warm = run_once(
            config, fsim::app::SimulationEngine::compiled, names);
        const auto debug = optimization == fsim::project::Optimization::o0
            ? std::optional<Capture> { run_once(
                  config, fsim::app::SimulationEngine::debug, names) }
            : std::nullopt;
        assert(reference.result.status == fsim::runtime::RunStatus::completed);
        if (reference.values != expected) {
            for (std::size_t index = 0; index < names.size(); ++index) {
                std::cerr << names[index] << "=" << reference.values[index]
                          << " expected " << expected[index] << '\n';
            }
        }
        assert(reference.values == expected);
        if (reference.values != compiled.values) {
            for (std::size_t index = 0; index < names.size(); ++index) {
                std::cerr << "compiled " << names[index] << '='
                          << compiled.values[index] << " reference "
                          << reference.values[index] << '\n';
            }
        }
        assert(reference.values == compiled.values);
        assert(reference.values == warm.values);
        if (debug)
            assert(reference.values == debug->values);
        assert((reference.local_values == std::vector<std::string> { "00000111", "00001001", "01000000000000000000000000000000", "100000" }));
        assert(reference.local_values == compiled.local_values);
        assert(reference.local_values == warm.local_values);
        assert(reference.vcd == compiled.vcd);
        assert(reference.vcd == warm.vcd);
        if (debug)
            assert(reference.vcd == debug->vcd);
        assert(reference.vcd.find("b01000000010000000000000000000000")
            != std::string::npos);
#if defined(FSIM_HAS_LLVM)
        assert(compiled.compiled_processes > 0);
        assert(compiled.compiled_modules > 0);
        assert(compiled.native_cache.misses == compiled.compiled_modules);
        assert(warm.native_cache.hits == warm.compiled_modules);
#endif
    }

    {
        const auto config = make_config(
            directory.path, sources, fsim::project::Optimization::o2);
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        assert(project && !diagnostics.has_error());
        std::size_t timing_operations { };
        for (const auto& process : project->design.processes()) {
            timing_operations += static_cast<std::size_t>(std::ranges::count_if(
                process.operations, [](const auto& operation) {
                    return fsim::runtime::simir::operation_get_if<
                               fsim::runtime::simir::VitalTimingCheck>(&operation)
                        != nullptr;
                }));
        }
        assert(timing_operations >= 7U);
        const auto encoded = fsim::app::serialize_runtime_state(
            project->design, diagnostics);
        assert(encoded && !diagnostics.has_error());
        const auto restored = fsim::app::deserialize_runtime_state(
            *encoded, "vital-runtime-state", diagnostics);
        assert(restored && !diagnostics.has_error());
        assert(fsim::app::serialize_runtime_state(*restored, diagnostics)
            == encoded);
        auto object_config = config;
        object_config.source_sets.clear();
        fsim::project::SourceSet object_sources;
        object_sources.language = fsim::project::Language::vhdl;
        object_sources.standard = "2008";
        object_sources.library = "work";
        object_sources.compilation_unit = "file";
        object_sources.files = sources;
        object_config.source_sets.push_back(std::move(object_sources));
        const auto object = directory.path / "vital.fsimobj";
        const auto artifact = directory.path / "vital.fsimdesign";
        const auto compiled_object = fsim::app::compile_artifact(
            object_config, object, diagnostics);
        if (!compiled_object) {
            for (const auto& diagnostic : diagnostics.diagnostics()) {
                std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
            }
        }
        assert(compiled_object);
        const std::array objects { object };
        auto elaborate_config = config;
        elaborate_config.source_sets.clear();
        const auto elaborated_artifact = fsim::app::elaborate_artifact(
            elaborate_config, objects, artifact, diagnostics);
        if (!elaborated_artifact) {
            for (const auto& diagnostic : diagnostics.diagnostics()) {
                std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
            }
        }
        assert(elaborated_artifact);
        auto loaded = fsim::app::load_design_artifact(artifact, diagnostics);
        assert(loaded && !diagnostics.has_error());
        fsim::app::Simulation simulation {
            std::move(*loaded), config.run.max_deltas,
            fsim::app::SimulationEngine::compiled
        };
        std::vector<fsim::runtime::simir::SignalId> artifact_signals;
        for (const auto& name : names) {
            const auto signal = simulation.find_signal(name);
            assert(signal);
            artifact_signals.push_back(*signal);
        }
        const auto result = simulation.run();
        assert(result.status == fsim::runtime::RunStatus::completed);
        std::vector<std::string> artifact_values;
        for (const auto signal : artifact_signals) {
            artifact_values.push_back(
                simulation.read_signal(signal).to_msb_string());
        }
        assert(artifact_values == expected);
    }

    {
        std::ofstream output { context, std::ios::app };
        output << "-- Task 8 context provenance edit\n";
        assert(output.good());
    }
    const auto edited = run_once(
        make_config(
            directory.path, sources, fsim::project::Optimization::o2),
        fsim::app::SimulationEngine::compiled, names);
    assert(edited.values == expected);
#if defined(FSIM_HAS_LLVM)
    assert(edited.native_cache.misses == edited.compiled_modules);
    assert(edited.native_cache.stores == edited.compiled_modules);
    assert(edited.native_cache.hits == 0);
#endif

    const auto expect_vital_failure = [&](const std::string_view declaration,
                                          const std::string_view assignment,
                                          const std::string_view code) {
        std::ofstream output { architecture };
        output << "context work.ieee_all;\n"
                  "architecture rtl of ieee_integration is\n  "
               << declaration
               << "\nbegin\n  " << assignment
               << "\nend architecture;\n";
        assert(output.good());
        output.close();
        fsim::diagnostic::Engine diagnostics;
        const auto rejected = fsim::app::build_project(
            make_config(
                directory.path, sources, fsim::project::Optimization::o0),
            diagnostics);
        assert(!rejected);
        const auto found = std::ranges::any_of(
            diagnostics.diagnostics(), [&](const auto& diagnostic) {
                return diagnostic.code == code;
            });
        if (!found) {
            std::cerr << "expected " << code << " for " << assignment << '\n';
            for (const auto& diagnostic : diagnostics.diagnostics()) {
                std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
            }
        }
        assert(found);
    };
    expect_vital_failure(
        "signal invalid : std_logic;", "invalid <= VitalBUF();",
        "FSIM-ELAB-VITAL-003");
    expect_vital_failure(
        "signal invalid : std_logic;",
        "invalid <= VitalExtendToFillDelay(\"01\")(0);",
        "FSIM-ELAB-VITAL-006");
    expect_vital_failure(
        "signal invalid : boolean;",
        "invalid <= VitalCalcDelay('1', '0', "
        "VitalExtendToFillDelay((-1 ns, 2 ns))) = 0 ns;",
        "FSIM-ELAB-VITAL-006");
    expect_vital_failure(
        "signal invalid : std_logic;",
        "invalid <= VitalTruthTable((('0')), \"0\");",
        "FSIM-ELAB-VITAL-008");
    expect_vital_failure(
        "signal invalid : std_logic;",
        "invalid <= VitalTruthTable((('Q', '1'), ('0', '0')), \"0\");",
        "FSIM-ELAB-VITAL-009");
    expect_vital_failure(
        "signal table_value : VitalTruthTableType(0 to 0, 0 to 1); "
        "signal invalid : std_logic;",
        "invalid <= VitalTruthTable(table_value, \"0\");",
        "FSIM-ELAB-VITAL-008");
    expect_vital_failure(
        "signal invalid : std_logic;",
        "invalid <= VitalMUX4(\"1010\", \"0\");",
        "FSIM-ELAB-VITAL-007");
    expect_vital_failure(
        "signal test_signal : std_logic;",
        "checker : process(test_signal) "
        "variable violation : std_logic := '0'; "
        "variable timing_data : VitalPeriodDataType := VitalPeriodDataInit; "
        "begin VitalPeriodPulseCheck(Violation => violation, "
        "PeriodData => timing_data, TestSignal => test_signal, "
        "Period => -1 ns); end process;",
        "FSIM-ELAB-VITAL-011");
    expect_vital_failure(
        "signal test_signal : std_logic;",
        "checker : process(test_signal) "
        "variable violation : std_logic := '0'; "
        "variable timing_data : VitalTimingDataType := VitalTimingDataInit; "
        "begin VitalPeriodPulseCheck(Violation => violation, "
        "PeriodData => timing_data, TestSignal => test_signal); end process;",
        "FSIM-ELAB-VITAL-012");
    expect_vital_failure(
        "signal test_signal : std_logic; signal ref_signal : std_logic;",
        "checker : process(test_signal, ref_signal) "
        "variable violation : std_logic := '0'; "
        "variable timing_data : VitalTimingDataType := VitalTimingDataInit; "
        "begin VitalSetupHoldCheck(Violation => violation, "
        "TimingData => timing_data, TestSignal => test_signal, "
        "RefSignal => ref_signal, RefTransition => 'Z'); end process;",
        "FSIM-ELAB-VITAL-011");
    expect_vital_failure(
        "signal data_in : std_logic_vector(0 downto 0);",
        "checker : process(data_in) "
        "variable result_value : std_logic := '0'; "
        "variable previous : std_logic_vector(0 downto 0) := \"X\"; "
        "begin VitalStateTable(Result => result_value, "
        "PreviousDataIn => previous, StateTable => (('0', '1')), "
        "DataIn => data_in); end process;",
        "FSIM-ELAB-VITAL-015");
    expect_vital_failure(
        "signal data_in : std_logic_vector(0 downto 0);",
        "checker : process(data_in) "
        "variable result_value : std_logic := '0'; "
        "variable previous : std_logic_vector(0 downto 0) := \"X\"; "
        "begin VitalStateTable(Result => result_value, "
        "PreviousDataIn => previous, StateTable => "
        "(('Z', '-', '1'), ('-', '-', 'S')), "
        "DataIn => data_in); end process;",
        "FSIM-ELAB-VITAL-016");
    expect_vital_failure(
        "signal data_in : std_logic_vector(0 downto 0);",
        "checker : process(data_in) "
        "variable result_value : std_logic := '0'; "
        "variable previous : bit_vector(0 downto 0) := \"0\"; "
        "begin VitalStateTable(Result => result_value, "
        "PreviousDataIn => previous, StateTable => (('-', '-', '1')), "
        "DataIn => data_in); end process;",
        "FSIM-ELAB-VITAL-014");
    std::cout
        << "FSIM-VHDL-OLDER-ENVIRONMENT-PASS "
           "revisions=1987/1993/2000/2002 "
           "packages=std_logic_signed/std_logic_unsigned/std_logic_arith/"
           "std_logic_misc provenance=revision/source-digest "
           "resources=as6g/delta1000/vcd64\n";
    return 0;
}
