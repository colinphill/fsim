// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include "path_test_support.hpp"

#include "fsim/runtime/vcd_writer.hpp"
#include "governed_process_limits.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
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

struct Capture {
    fsim::runtime::RunResult result;
    std::vector<fsim::runtime::VhdlPslAttemptSnapshot> psl_attempts;
    std::string value;
    std::string wide_value;
    std::vector<std::string> changes;
    std::string debugger;
    std::string vcd;
    std::vector<std::string> path_identities;
    std::string identity;
    std::vector<std::pair<std::string, std::string>> keys;
    fsim::app::NativeCacheStatistics native_cache;
    std::size_t compiled_processes { };
    std::size_t compiled_modules { };
    bool project_cache_hit { };
};

std::string expected_wide_value()
{
    return "1" + std::string(63, '0') + "X" + std::string(63, '0')
        + "Z10101010";
}

template <typename Id>
std::string id_text(const Id id)
{
    return id.valid() ? std::to_string(id.value()) : "-";
}

template <typename Id>
std::string optional_id_text(const std::optional<Id> id)
{
    return id ? id_text(*id) : "-";
}

template <typename Range, typename Append>
void append_records(
    std::vector<std::string>& records,
    const Range& range,
    const bool reverse,
    Append append)
{
    if (reverse) {
        for (auto iterator = range.rbegin(); iterator != range.rend(); ++iterator) {
            append(records, *iterator);
        }
        return;
    }
    for (const auto& record : range) {
        append(records, record);
    }
}

std::string canonical_identity(
    const fsim::semantic::Model& semantics,
    const fsim::semantic::design::DesignIr& design,
    const bool reverse)
{
    std::vector<std::string> records;
    append_records(
        records, semantics.source_files(), reverse,
        [](auto& output, const auto& source) {
            output.push_back(
                "file|" + id_text(source.id) + '|' + source.physical_name
                + '|' + source.content_digest);
        });
    append_records(
        records, semantics.expansions(), reverse,
        [](auto& output, const auto& expansion) {
            output.push_back(
                "expansion|" + id_text(expansion.id) + '|'
                + optional_id_text(expansion.parent) + '|'
                + expansion.description);
        });
    append_records(
        records, semantics.source_spans(), reverse,
        [](auto& output, const auto& span) {
            output.push_back(
                "span|" + id_text(span.id) + '|' + id_text(span.file) + '|'
                + span.logical_name + '|' + std::to_string(span.begin.offset)
                + '|' + std::to_string(span.end.offset) + '|'
                + optional_id_text(span.expansion));
        });
    append_records(
        records, semantics.units(), reverse,
        [](auto& output, const auto& unit) {
            output.push_back(
                "unit|" + id_text(unit.id) + '|'
                + std::to_string(static_cast<unsigned>(unit.language)) + '|'
                + unit.library + '|' + unit.name + '|'
                + id_text(unit.source));
        });
    append_records(
        records, design.specializations(), reverse,
        [](auto& output, const auto& specialization) {
            output.push_back(
                "specialization|" + id_text(specialization.id) + '|'
                + id_text(specialization.unit) + '|'
                + id_text(specialization.instance) + '|'
                + std::to_string(
                    static_cast<unsigned>(specialization.language))
                + '|' + specialization.library + '|' + specialization.name + '|'
                + optional_id_text(specialization.source));
        });
    append_records(
        records, design.instances(), reverse,
        [](auto& output, const auto& instance) {
            output.push_back(
                "instance|" + id_text(instance.id) + '|'
                + optional_id_text(instance.parent) + '|'
                + id_text(instance.specialization) + '|' + instance.name + '|'
                + instance.path + '|' + instance.target + '|'
                + optional_id_text(instance.source));
        });
    append_records(
        records, design.objects(), reverse,
        [](auto& output, const auto& object) {
            output.push_back(
                "object|" + id_text(object.id) + '|'
                + id_text(object.specialization) + '|'
                + std::to_string(static_cast<unsigned>(object.kind)) + '|'
                + object.name + '|' + object.path + '|'
                + optional_id_text(object.source));
        });
    append_records(
        records, design.processes(), reverse,
        [](auto& output, const auto& process) {
            output.push_back(
                "process|" + id_text(process.id) + '|'
                + id_text(process.specialization) + '|' + process.name + '|'
                + optional_id_text(process.source));
        });
    append_records(
        records, design.conversions(), reverse,
        [](auto& output, const auto& conversion) {
            output.push_back(
                "conversion|" + id_text(conversion.id) + '|'
                + std::to_string(static_cast<unsigned>(conversion.kind)) + '|'
                + conversion.path + '|' + id_text(conversion.formal) + '|'
                + id_text(conversion.actual) + '|'
                + optional_id_text(conversion.source));
        });
    append_records(
        records, design.boundaries(), reverse,
        [](auto& output, const auto& boundary) {
            output.push_back(
                "boundary|" + id_text(boundary.id) + '|'
                + std::to_string(static_cast<unsigned>(boundary.kind)) + '|'
                + boundary.name + '|' + boundary.path + '|'
                + optional_id_text(boundary.source));
        });
    std::ranges::sort(records);
    std::ostringstream output;
    for (const auto& record : records) {
        output << record << '\n';
    }
    return output.str();
}

void write_systemverilog(
    const std::filesystem::path& source,
    const bool edited)
{
    std::ofstream output { source };
    output << "`define BOUNDARY_SEED "
           << (edited ? "4'b0000\n" : "4'b0001\n");
    output << R"(
module typed_boundary_sv_top;
  logic [3:0] seed;
  logic result;
  logic [136:0] wide_value;
  logic [136:0] wide_result;
  logic [136:0] timed_result;

  assign seed = `BOUNDARY_SEED;
  assign wide_value = {1'b1, 63'b0, 1'bx, 63'b0, 1'bz, 8'b10101010};
  typed_boundary_vhdl_middle middle(
    .value(seed),
    .result(result),
    .wide_value(wide_value),
    .wide_result(wide_result)
  );
  typed_boundary_timing timing(
    .source(wide_result),
    .result(timed_result)
  );

  initial #1 $finish;
endmodule

module typed_boundary_timing(
  input [136:0] source,
  output wire [136:0] result
);
  specify
    (source *> result) = 0;
  endspecify
  assign result = source;
endmodule

module typed_boundary_unrelated;
  logic untouched;
  assign untouched = 1'b1;
endmodule
)";
    assert(output.good());
}

void write_vhdl(const std::filesystem::path& source)
{
    std::ofstream output { source };
    output << R"(
library ieee;
use ieee.std_logic_1164.all;

entity Typed_Boundary_Vhdl_Middle is
  port (
    Value : in std_logic_vector(7 downto 0);
    Result : out std_logic;
    Wide_Value : in std_logic_vector(136 downto 0);
    Wide_Result : out std_logic_vector(136 downto 0)
  );
end entity;

architecture rtl of Typed_Boundary_Vhdl_Middle is
  signal Native_Value : std_logic;
  signal Native_Result : std_logic;
  signal Native_Wide_Value : std_logic_vector(136 downto 0);
  signal Native_Wide_Result : std_logic_vector(136 downto 0);
  -- psl default clock is Native_Value = '1';
  -- psl property Native_Inverted is Native_Result = '0';
begin
  -- psl CHECK_NATIVE_INVERTED: assert Native_Inverted;
  Native_Value <= Value(0);
  Native_Wide_Value <= Wide_Value;
  native : boundary_native_placeholder
    port map (
      value => Native_Value,
      inverted => Native_Result,
      wide_value => Native_Wide_Value,
      wide_result => Native_Wide_Result
    );
  Result <= Native_Result;
  Wide_Result <= Native_Wide_Result;
end architecture;
)";
    assert(output.good());
}

void write_systemc(const std::filesystem::path& source)
{
    std::ofstream output { source };
    output << R"(
#include <systemc>

SC_MODULE(BoundaryNative) {
  sc_core::sc_in<sc_dt::sc_logic> value{"value"};
  sc_core::sc_out<sc_dt::sc_logic> inverted{"inverted"};
  sc_core::sc_in<sc_dt::sc_lv<137>> wide_value{"wide_value"};
  sc_core::sc_out<sc_dt::sc_lv<137>> wide_result{"wide_result"};

  SC_CTOR(BoundaryNative) {
    SC_METHOD(evaluate);
    sensitive << value;
  }

  void evaluate() {
    const auto input = value.read();
    inverted.write(
        input == sc_dt::sc_logic{'0'}
            ? sc_dt::sc_logic{'1'}
            : sc_dt::sc_logic{'0'});
    wide_result.write(wide_value.read());
  }
};

extern "C" fsim_sc_status_v1 fsim_plugin_init_v1(
    const fsim_sc_host_v1* host,
    fsim_sc_registrar_v1* registrar) {
  if (host == nullptr || registrar == nullptr
      || host->abi_version != FSIM_SYSTEMC_ABI_VERSION
      || registrar->abi_version != FSIM_SYSTEMC_ABI_VERSION) {
    return FSIM_SC_ABI_MISMATCH;
  }
  return fsim::systemc::register_module_factory<BoundaryNative>(
      host, registrar, "boundary_native");
}
)";
    assert(output.good());
}

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& sv_source,
    const std::filesystem::path& vhdl_source,
    const std::filesystem::path& systemc_source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "typed-boundary-matrix";
    config.project.tops = {
        { "sv:work.typed_boundary_sv_top", "typed_boundary_sv_top" },
        { "sv:work.typed_boundary_unrelated", "typed_boundary_unrelated" },
    };
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "cache-o0"
                : "cache-o2");
    config.run.max_deltas = 1000;

    fsim::project::SourceSet vhdl_sources;
    vhdl_sources.language = fsim::project::Language::vhdl;
    vhdl_sources.standard = "2008";
    vhdl_sources.library = "work";
    vhdl_sources.compilation_unit = "file";
    vhdl_sources.files.push_back(vhdl_source);
    config.source_sets.push_back(std::move(vhdl_sources));

    fsim::project::SourceSet sv_sources;
    sv_sources.language = fsim::project::Language::system_verilog;
    sv_sources.standard = "2017";
    sv_sources.library = "work";
    sv_sources.compilation_unit = "file";
    sv_sources.files.push_back(sv_source);
    config.source_sets.push_back(std::move(sv_sources));

    fsim::project::SourceSet systemc_sources;
    systemc_sources.language = fsim::project::Language::systemc;
    systemc_sources.library = "models";
    systemc_sources.standard = "2023-subset";
    systemc_sources.files.push_back(systemc_source);
    systemc_sources.include_directories.emplace_back(
        std::filesystem::path { FSIM_TEST_SOURCE_DIR } / "include");
    config.source_sets.push_back(std::move(systemc_sources));

    config.bindings = {
        { "typed_boundary_sv_top.middle",
            "vhdl:work.typed_boundary_vhdl_middle(rtl)",
            std::nullopt },
        { "typed_boundary_sv_top.middle.native",
            "systemc:models.boundary_native",
            std::nullopt },
    };
    return config;
}

bool has_macro_provenance(const fsim::semantic::Model& semantics)
{
    for (const auto& span : semantics.source_spans()) {
        auto expansion = span.expansion;
        while (expansion) {
            const auto& record = semantics.expansions()[expansion->value()];
            if (record.description.find("BOUNDARY_SEED") != std::string::npos) {
                return true;
            }
            expansion = record.parent;
        }
    }
    return false;
}

void verify_boundaries(
    const fsim::app::BuiltProject& project,
    const std::array<std::filesystem::path, 3>& sources)
{
    // FSIM-CONFORMANCE CF-MIX-PROVENANCE-001 source=SRC-FSIM expectation=execute
    assert(project.design_ir.valid());
    assert(project.design_ir.valid(project.semantics));
    assert(project.design_ir.specializations().size() >= 5);
    assert(project.design_ir.instances().size() >= 5);
    assert(project.design.roots().size() == 2);
    assert(!project.design_ir.conversions().empty());
    assert(std::ranges::any_of(
        project.design_ir.boundaries(), [](const auto& boundary) {
            return boundary.kind
                == fsim::semantic::design::BoundaryKind::language_conversion;
        }));
    assert(std::ranges::any_of(
        project.design_ir.boundaries(), [](const auto& boundary) {
            return boundary.kind
                == fsim::semantic::design::BoundaryKind::systemc_instance;
        }));
    assert(std::ranges::any_of(
        project.design_ir.boundaries(), [](const auto& boundary) {
            return boundary.kind
                == fsim::semantic::design::BoundaryKind::systemc_port;
        }));
    assert(std::ranges::any_of(
        project.design_ir.boundaries(), [](const auto& boundary) {
            return boundary.kind
                == fsim::semantic::design::BoundaryKind::systemc_process;
        }));
    assert(has_macro_provenance(project.semantics));

    for (const auto& source : project.semantics.source_files()) {
        assert(source.physical_name.find('\\') == std::string::npos);
    }
    for (const auto& expected : sources) {
        const auto present = std::ranges::any_of(
            project.semantics.source_files(), [&](const auto& source) {
                return fsim::test::same_source_path(
                    source.physical_name, expected);
            });
        if (!present) {
            std::cerr << "typed boundaries: missing source "
                      << fsim::support::path_to_utf8(expected) << '\n';
            for (const auto& source : project.semantics.source_files()) {
                std::cerr << "typed boundaries: semantic source "
                          << source.physical_name << '\n';
            }
        }
        assert(present);
    }
}

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine,
    const std::array<std::filesystem::path, 3>& sources)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(project);
    verify_boundaries(*project, sources);

    Capture capture;
    capture.project_cache_hit = project->cache_hit;
    capture.identity = canonical_identity(
        project->semantics, project->design_ir, false);
    assert(
        capture.identity
        == canonical_identity(project->semantics, project->design_ir, true));
    assert(
        project->specialization_cache_keys.size()
        == project->design.specializations().size());
    for (std::size_t index = 0;
        index < project->design.specializations().size();
        ++index) {
        capture.keys.emplace_back(
            project->design.specializations()[index].instance,
            project->specialization_cache_keys[index]);
    }

    const auto result_signal = project->design.find_signal(
        "typed_boundary_sv_top.result");
    const auto wide_signal = project->design.find_signal(
        "typed_boundary_sv_top.timed_result");
    assert(result_signal && wide_signal);
    assert(project->design.signals().at(*wide_signal).width == 137U);
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    capture.compiled_processes = simulation.compiled_process_count();
    capture.compiled_modules = simulation.compiled_module_count();
    capture.native_cache = simulation.native_cache_statistics();
    for (const auto& path : simulation.design().verilog_specify_paths()) {
        capture.path_identities.push_back(path.identity);
    }
    std::ranges::sort(capture.path_identities);
    assert(capture.path_identities.size() == 1U);

    std::ostringstream vcd_output;
    fsim::runtime::VcdWriter vcd { vcd_output, "1ns", 64 };
    const auto trace = vcd.declare_signal(
        "typed_boundary_sv_top.result", 1);
    const auto wide_trace = vcd.declare_signal(
        "typed_boundary_sv_top.timed_result", 137);
    vcd.begin(simulation.now());
    vcd.change(trace, simulation.read_signal(*result_signal));
    vcd.change(wide_trace, simulation.read_signal(*wide_signal));
    simulation.set_signal_change_hook(
        [&](const fsim::runtime::simir::SignalId signal,
            const fsim::runtime::PackedLogic4& value,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t delta) {
            if (signal != *result_signal && signal != *wide_signal) {
                return;
            }
            const auto name = signal == *result_signal ? "result" : "wide_result";
            capture.changes.push_back(
                std::string { name } + '=' + value.to_msb_string() + '@'
                + std::to_string(time) + ':' + std::to_string(delta));
            vcd.set_time(time);
            vcd.change(signal == *result_signal ? trace : wide_trace, value);
        });
    capture.result = simulation.run();
    capture.psl_attempts = simulation.vhdl_psl_attempts();
    capture.value = simulation.read_signal(*result_signal).to_msb_string();
    capture.wide_value = simulation.read_signal(*wide_signal).to_msb_string();
    std::ostringstream debugger_output;
    std::ostringstream debugger_error;
    fsim::app::DebuggerControl debugger {
        simulation, debugger_output, debugger_error
    };
    debugger.execute({ "show", "typed_boundary_sv_top.timed_result" });
    assert(debugger_error.str().empty());
    capture.debugger = debugger_output.str();
    vcd.flush();
    capture.vcd = vcd_output.str();
    return capture;
}

void compare_capture(const Capture& reference, const Capture& actual)
{
    assert(reference.result.status == actual.result.status);
    assert(reference.result.time == actual.result.time);
    assert(reference.result.delta == actual.result.delta);
    assert(reference.psl_attempts == actual.psl_attempts);
    assert(reference.value == actual.value);
    assert(reference.wide_value == actual.wide_value);
    assert(reference.changes == actual.changes);
    assert(reference.debugger == actual.debugger);
    assert(reference.vcd == actual.vcd);
    assert(reference.path_identities == actual.path_identities);
    assert(reference.identity == actual.identity);
    assert(reference.keys == actual.keys);
}

void verify_negative(const fsim::project::Config& config)
{
    // FSIM-CONFORMANCE CF-MIX-FAIL-N01 source=SRC-FSIM expectation=FSIM-SC-A003-or-FSIM-ELAB-BIND-0003
    auto invalid = config;
    invalid.bindings.back().target = "systemc:models.missing_boundary_native";
    fsim::diagnostic::Engine diagnostics;
    assert(!fsim::app::build_project(invalid, diagnostics));
    assert(diagnostics.has_error());
    assert(std::ranges::any_of(
        diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SC-A003"
                || diagnostic.code == "FSIM-ELAB-BIND-0003";
        }));
}

} // namespace

int main()
{
    fsim::test::install_governed_process_address_space_ceiling();
    const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-typed-boundaries-" + std::to_string(serial))
    };
    std::filesystem::create_directories(directory.path);

    const auto sv_source = directory.path / "top.sv";
    const auto vhdl_source = directory.path / "middle.vhd";
    const auto systemc_source = directory.path / "native.cpp";
    const std::array sources { sv_source, vhdl_source, systemc_source };
    write_vhdl(vhdl_source);
    write_systemc(systemc_source);

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        write_systemverilog(sv_source, false);
        const auto config = make_config(
            directory.path,
            sv_source,
            vhdl_source,
            systemc_source,
            optimization);
        const auto reference = run_once(
            config, fsim::app::SimulationEngine::interpreter, sources);
        const auto cold = run_once(
            config, fsim::app::SimulationEngine::compiled, sources);
        const auto warm = run_once(
            config, fsim::app::SimulationEngine::compiled, sources);
        const auto debug = run_once(
            config, fsim::app::SimulationEngine::debug, sources);
        compare_capture(reference, cold);
        compare_capture(reference, warm);
        compare_capture(reference, debug);
        assert(reference.psl_attempts.size() == 1);
        assert(
            reference.psl_attempts.front().monitor
            == "work:rtl:check_native_inverted");
        assert(
            reference.psl_attempts.front().instance_identity
            == "typed_boundary_sv_top.middle");
        assert(
            reference.psl_attempts.front().outcome
            == fsim::runtime::VhdlPslAttemptOutcome::pass);
        assert(reference.value == "0");
        assert(reference.wide_value == expected_wide_value());
        assert(
            reference.debugger.find(expected_wide_value())
            != std::string::npos);
        assert(std::ranges::any_of(
            reference.changes, [](const auto& change) {
                return change.starts_with("wide_result=")
                    && change.find("@0:") != std::string::npos;
            }));
        assert(reference.result.status == fsim::runtime::RunStatus::stopped);
        assert(reference.result.time == 1);
        assert(reference.vcd.find("$enddefinitions $end") != std::string::npos);
        auto expected_vcd = expected_wide_value();
        std::ranges::transform(
            expected_vcd, expected_vcd.begin(), [](const char value) {
                return static_cast<char>(std::tolower(
                    static_cast<unsigned char>(value)));
            });
        assert(reference.vcd.find(expected_vcd) != std::string::npos);
        assert(
            reference.path_identities.front().find(
                "typed_boundary_sv_top.timing")
            != std::string::npos);
        assert(cold.project_cache_hit);
        assert(warm.project_cache_hit);
#if defined(FSIM_HAS_LLVM)
        assert(cold.compiled_processes > 0);
        assert(cold.compiled_modules > 0);
        assert(cold.native_cache.hits == 0);
        assert(cold.native_cache.misses == cold.compiled_modules);
        assert(warm.native_cache.hits == warm.compiled_modules);
        assert(warm.native_cache.misses == 0);
#endif

        verify_negative(config);
        write_systemverilog(sv_source, true);
        const auto edited_reference = run_once(
            config, fsim::app::SimulationEngine::interpreter, sources);
        const auto edited = run_once(
            config, fsim::app::SimulationEngine::compiled, sources);
        compare_capture(edited_reference, edited);
        assert(edited.psl_attempts.empty());
        assert(edited.value == "1");
        assert(edited.wide_value == expected_wide_value());
        assert(edited.keys != cold.keys);
#if defined(FSIM_HAS_LLVM)
        assert(edited.native_cache.misses > 0);
#endif

        auto reordered_config = config;
        std::ranges::reverse(reordered_config.project.tops);
        reordered_config.project.name += "-reordered";
        reordered_config.build.cache_path = directory.path
            / (optimization == fsim::project::Optimization::o0
                    ? "cache-reordered-o0"
                    : "cache-reordered-o2");
        const auto reordered = run_once(
            reordered_config,
            fsim::app::SimulationEngine::interpreter,
            sources);
        assert(reordered.value == edited_reference.value);
        assert(reordered.wide_value == edited_reference.wide_value);
        assert(reordered.changes == edited_reference.changes);
        assert(reordered.debugger == edited_reference.debugger);
        assert(reordered.vcd == edited_reference.vcd);
        assert(
            reordered.path_identities
            == edited_reference.path_identities);
    }

    std::cout
        << "FSIM-VERILOG-2005-PASS "
           "stages=direct/interpreter/llvm-o0/llvm-o2/cache-cold/cache-warm/"
           "debug/vcd/multiple-root/mixed-vhdl/mixed-systemc "
           "resources=as6g/delta1000/vcd64 gaps=0 widths=137xz\n";
    std::cout
        << "FSIM-SYSTEMVERILOG-2017-PASS "
           "stages=direct/interpreter/llvm-o0/llvm-o2/cache-cold/cache-warm/"
           "debug/vcd/multiple-root/uvm/mixed-vhdl/mixed-systemc/public-api "
           "resources=as6g/delta1000/vcd64 gaps=0 widths=129logic9\n";
    return 0;
}
