// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <system_error>
#include <vector>

#include "application_test_support.hpp"
#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/app/sdf_phase_persistence.hpp"
#include "fsim/artifact/design.hpp"
#include "fsim/artifact/object.hpp"
#include "fsim/library/portable_unit.hpp"
#include "fsim/runtime/fst_change_encoder.hpp"
#include "fsim/runtime/fst_reader.hpp"
#include "fsim/runtime/fst_value_encoder.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"
#include "fsim/systemc/incremental.hpp"
#include "fsim/systemc/scv.hpp"

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define FSIM_TEST_ASAN_ENABLED 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) && !defined(FSIM_TEST_ASAN_ENABLED)
#define FSIM_TEST_ASAN_ENABLED 1
#endif

namespace fsim::test {
namespace {

    std::string read_binary_file(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        assert(input);
        const std::string result { std::istreambuf_iterator<char> { input },
            std::istreambuf_iterator<char> { } };
        assert(!input.bad());
        return result;
    }

    std::uint64_t read_be64(const std::string& bytes, const std::size_t offset)
    {
        assert(offset + 8U <= bytes.size());
        std::uint64_t result = 0;
        for (std::size_t index = 0; index < 8U; ++index) {
            result = (result << 8U) | static_cast<unsigned char>(bytes[offset + index]);
        }
        return result;
    }

    std::uint64_t read_varint(const std::string& bytes, std::size_t& offset)
    {
        std::uint64_t result = 0;
        unsigned shift = 0;
        while (true) {
            assert(offset < bytes.size() && shift < 64U);
            const auto byte = static_cast<unsigned char>(bytes[offset++]);
            result |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
            if ((byte & 0x80U) == 0U) {
                return result;
            }
            shift += 7U;
        }
    }

    struct FstInitialFrame {
        std::uint64_t uncompressed { };
        std::uint64_t stored { };
        std::uint64_t handles { };
        std::uint64_t waves { };
    };

    FstInitialFrame fst_initial_frame(const std::string& bytes)
    {
        assert(bytes.size() > 363U);
        assert(static_cast<unsigned char>(bytes.at(330U)) == 8U);
        std::size_t offset = 363U;
        const auto uncompressed = read_varint(bytes, offset);
        const auto stored = read_varint(bytes, offset);
        const auto handles = read_varint(bytes, offset);
        assert(stored != 0U && offset + stored <= bytes.size());
        if (uncompressed != stored) {
            assert(static_cast<unsigned char>(bytes.at(offset)) == 0x78U);
        }
        FstInitialFrame result { uncompressed, stored, handles, 0U };
        offset += static_cast<std::size_t>(stored);
        result.waves = read_varint(bytes, offset);
        return result;
    }

    std::vector<library::PortablePayload> object_payloads(
        const std::filesystem::path& directory,
        const artifact::ObjectMetadata& metadata)
    {
        std::vector<library::PortablePayload> result;
        for (const auto& source : metadata.sources) {
            result.push_back(
                { source.artifact, read_binary_file(directory / source.artifact) });
        }
        for (const auto& unit : metadata.units) {
            result.push_back(
                { unit.artifact, read_binary_file(directory / unit.artifact) });
        }
        return result;
    }

    void copy_tree(const std::filesystem::path& source,
        const std::filesystem::path& destination)
    {
        std::filesystem::create_directories(destination);
        for (const auto& entry :
            std::filesystem::recursive_directory_iterator(source)) {
            const auto relative = std::filesystem::relative(entry.path(), source);
            if (entry.is_directory()) {
                std::filesystem::create_directories(destination / relative);
            } else {
                std::filesystem::copy_file(entry.path(), destination / relative);
            }
        }
    }

    void make_tree_writable(const std::filesystem::path& root)
    {
        for (const auto& entry :
            std::filesystem::recursive_directory_iterator(root)) {
            std::filesystem::permissions(entry.path(),
                std::filesystem::perms::owner_all,
                std::filesystem::perm_options::add);
        }
        std::filesystem::permissions(root, std::filesystem::perms::owner_all,
            std::filesystem::perm_options::add);
    }

    void make_tree_read_only(const std::filesystem::path& root)
    {
        for (const auto& entry :
            std::filesystem::recursive_directory_iterator(root)) {
            const auto permissions = entry.is_directory()
                ? std::filesystem::perms::owner_read
                    | std::filesystem::perms::owner_exec
                    | std::filesystem::perms::group_read
                    | std::filesystem::perms::group_exec
                    | std::filesystem::perms::others_read
                    | std::filesystem::perms::others_exec
                : std::filesystem::perms::owner_read
                    | std::filesystem::perms::group_read
                    | std::filesystem::perms::others_read;
            std::filesystem::permissions(entry.path(), permissions,
                std::filesystem::perm_options::replace);
        }
        std::filesystem::permissions(root,
            std::filesystem::perms::owner_read
                | std::filesystem::perms::owner_exec
                | std::filesystem::perms::group_read
                | std::filesystem::perms::group_exec
                | std::filesystem::perms::others_read
                | std::filesystem::perms::others_exec,
            std::filesystem::perm_options::replace);
    }

} // namespace

void ApplicationTestFixture::test_non_project_cli()
{
    const auto object = directory / "unit.fsimobj";
    const auto extra_object = directory / "extra.fsimobj";
    const auto extra_source = directory / "extra.sv";
    const auto design = directory / "design.fsimdesign";
    const auto aot_design = directory / "aot-design.fsimdesign";
    const auto selected_aot_design
        = directory / "selected-aot-design.fsimdesign";
    const auto final_aot_design = directory / "final-aot-design.fsimdesign";
    const auto trace = directory / "phase.fst";
    const auto fst_trace = directory / "phase.fst";
    const auto conflict_trace = directory / "phase.vcd";
    const auto consumer_cache = directory / "consumer-cache";
    const auto consumer_file_root = directory / "consumer-files";
    const auto source_text = fsim::support::path_to_utf8(source);
    const auto object_text = fsim::support::path_to_utf8(object);
    const auto extra_object_text = fsim::support::path_to_utf8(extra_object);
    const auto extra_source_text = fsim::support::path_to_utf8(extra_source);
    const auto design_text = fsim::support::path_to_utf8(design);
    const auto aot_design_text = fsim::support::path_to_utf8(aot_design);
    const auto selected_aot_design_text
        = fsim::support::path_to_utf8(selected_aot_design);
    const auto final_aot_design_text
        = fsim::support::path_to_utf8(final_aot_design);
    const auto trace_text = fsim::support::path_to_utf8(trace);
    const auto fst_trace_text = fsim::support::path_to_utf8(fst_trace);
    const auto conflict_trace_text = fsim::support::path_to_utf8(conflict_trace);
    const auto consumer_cache_text = fsim::support::path_to_utf8(consumer_cache);
    const auto consumer_file_root_text = fsim::support::path_to_utf8(consumer_file_root);

    const std::vector<const char*> compile_arguments { "fsim",
        "compile",
        "--lang",
        "systemverilog",
        "--standard",
        "2017",
        "--library",
        "work",
        "--output",
        object_text.c_str(),
        "--trace-output",
        fst_trace_text.c_str(),
        "--trace-format",
        "fst",
        "--trace-compression",
        "deterministic",
        "--trace-select",
        "primary.*",
        "--trace-report-limit",
        "12",
        "--trace-lifecycle",
        "configured",
        source_text.c_str() };
    {
        std::ofstream extra_output(extra_source);
        extra_output << "`define LEAK\nmodule extra; endmodule\n";
    }
    const std::vector<const char*> extra_compile_arguments {
        "fsim",
        "compile",
        "--lang",
        "systemverilog",
        "--standard",
        "2017",
        "--library",
        "work",
        "--output",
        extra_object_text.c_str(),
        extra_source_text.c_str()
    };
    diagnostic::Engine compile_diagnostics;
    const auto compile = cli::parse_arguments(static_cast<int>(compile_arguments.size()),
        compile_arguments.data(), compile_diagnostics);
    assert(compile && !compile_diagnostics.has_error());
    assert(compile->command == cli::Command::compile);
    assert(compile->artifact_output == object);
    assert(compile->files == std::vector { source });
    assert(compile->trace_file == fst_trace);
    assert(compile->trace_format == project::TraceFormat::fst);
    assert(compile->trace_compression == project::TraceCompression::deterministic);
    assert(compile->trace_filters == std::vector<std::string> { "primary.*" });
    assert(compile->trace_report_limit == 12U);
    assert(compile->trace_enabled == true);
    const std::vector<const char*> uvm_release_arguments {
        "fsim", "check", "--uvm-release", "uvm-2020.3.1", source_text.c_str()
    };
    diagnostic::Engine uvm_release_diagnostics;
    const auto uvm_release = cli::parse_arguments(
        static_cast<int>(uvm_release_arguments.size()),
        uvm_release_arguments.data(), uvm_release_diagnostics);
    assert(uvm_release && !uvm_release_diagnostics.has_error());
    assert(uvm_release->uvm_release == project::SystemVerilogUvmRelease::ieee_1800_2_2020_3_1);
    const std::vector<const char*> invalid_uvm_release_arguments {
        "fsim", "check", "--uvm-release", "2024", source_text.c_str()
    };
    diagnostic::Engine invalid_uvm_release_diagnostics;
    assert(!cli::parse_arguments(
        static_cast<int>(invalid_uvm_release_arguments.size()),
        invalid_uvm_release_arguments.data(), invalid_uvm_release_diagnostics));

    const std::vector<const char*> elaborate_arguments { "fsim",
        "elaborate",
        "--object",
        object_text.c_str(),
        "--top",
        "primary=sv:work.tb",
        "--search-library",
        "vendor",
        "--output",
        design_text.c_str(),
        "--delay-mode",
        "max",
        "--trace-output",
        fst_trace_text.c_str(),
        "--trace-format",
        "fst",
        "--trace-compression",
        "deterministic",
        "--trace-select",
        "primary.*",
        "--trace-report-limit",
        "12",
        "--trace-lifecycle",
        "configured",
        "--seed",
        "7" };
    diagnostic::Engine elaborate_diagnostics;
    const auto elaborate = cli::parse_arguments(static_cast<int>(elaborate_arguments.size()),
        elaborate_arguments.data(), elaborate_diagnostics);
    assert(elaborate && !elaborate_diagnostics.has_error());
    assert(elaborate->command == cli::Command::elaborate);
    assert(elaborate->objects == std::vector { object });
    assert(elaborate->artifact_output == design);
    assert(elaborate->tops.size() == 1);
    assert(elaborate->tops.front().alias == "primary");
    assert(elaborate->trace_file == fst_trace);
    assert(elaborate->trace_format == project::TraceFormat::fst);
    assert(elaborate->trace_compression == project::TraceCompression::deterministic);

    const std::vector<const char*> aot_parse_arguments {
        "fsim", "elaborate",
        "--object", object_text.c_str(),
        "--top", "primary=sv:work.tb",
        "--output", aot_design_text.c_str(),
        "--aot", "--aot-scope", "all",
        "--cache", consumer_cache_text.c_str()
    };
    diagnostic::Engine aot_parse_diagnostics;
    const auto aot_parse = cli::parse_arguments(
        static_cast<int>(aot_parse_arguments.size()),
        aot_parse_arguments.data(), aot_parse_diagnostics);
    assert(aot_parse && !aot_parse_diagnostics.has_error());
    assert(aot_parse->aot == true);
    assert(aot_parse->aot_scope == cli::AotScope::all);
    assert(aot_parse->cache_directory == consumer_cache);
    const std::vector<const char*> disabled_aot_arguments {
        "fsim", "elaborate",
        "--object", object_text.c_str(),
        "--top", "primary=sv:work.tb",
        "--output", selected_aot_design_text.c_str(),
        "--aot", "--no-aot"
    };
    diagnostic::Engine disabled_aot_diagnostics;
    const auto disabled_aot = cli::parse_arguments(
        static_cast<int>(disabled_aot_arguments.size()),
        disabled_aot_arguments.data(), disabled_aot_diagnostics);
    assert(disabled_aot && !disabled_aot_diagnostics.has_error());
    assert(disabled_aot->aot == false);
    const std::vector<const char*> invalid_aot_scope_arguments {
        "fsim", "elaborate",
        "--object", object_text.c_str(),
        "--top", "primary=sv:work.tb",
        "--output", design_text.c_str(),
        "--aot-scope", "all"
    };
    diagnostic::Engine invalid_aot_scope_diagnostics;
    assert(!cli::parse_arguments(
        static_cast<int>(invalid_aot_scope_arguments.size()),
        invalid_aot_scope_arguments.data(), invalid_aot_scope_diagnostics));

    const std::vector<const char*> simulate_arguments {
        "fsim", "simulate",
        "--design", design_text.c_str(),
        "--engine", "compiled",
        "--optimization", "O0",
        "--duration", "10ns",
        "--max-deltas", "1000",
        "--trace", trace_text.c_str(),
        "--trace-format", "fst",
        "--trace-compression", "deterministic",
        "--trace-select", "primary.*",
        "--trace-report-limit", "12",
        "--trace-lifecycle", "configured",
        "--cache", consumer_cache_text.c_str(),
        "--file-root", consumer_file_root_text.c_str()
    };
    diagnostic::Engine simulate_diagnostics;
    const auto simulate = cli::parse_arguments(static_cast<int>(simulate_arguments.size()),
        simulate_arguments.data(), simulate_diagnostics);
    assert(simulate && !simulate_diagnostics.has_error());
    assert(simulate->command == cli::Command::simulate);
    assert(simulate->design == design);
    assert(simulate->engine == "compiled");
    assert(simulate->optimization == project::Optimization::o0);
    assert(simulate->trace_filters == std::vector<std::string> { "primary.*" });
    assert(simulate->trace_format == project::TraceFormat::fst);
    assert(simulate->trace_compression == project::TraceCompression::deterministic);
    assert(simulate->trace_report_limit == 12U);
    assert(simulate->trace_enabled == true);
    assert(simulate->cache_directory == consumer_cache);
    assert(simulate->file_root == consumer_file_root);
    assert(!simulate->compiled_processes.has_value());
    const std::vector<const char*> selected_simulate_arguments {
        "fsim", "simulate", "--design", design_text.c_str(),
        "--compiled-processes", "selected"
    };
    diagnostic::Engine selected_simulate_diagnostics;
    const auto selected_simulate = cli::parse_arguments(
        static_cast<int>(selected_simulate_arguments.size()),
        selected_simulate_arguments.data(), selected_simulate_diagnostics);
    assert(selected_simulate && !selected_simulate_diagnostics.has_error());
    assert(selected_simulate->compiled_processes
        == cli::CompiledProcessPolicy::selected);
    const std::vector<const char*> invalid_interpreter_policy_arguments {
        "fsim", "simulate", "--design", design_text.c_str(),
        "--engine", "interpreter", "--compiled-processes", "all"
    };
    diagnostic::Engine invalid_interpreter_policy_diagnostics;
    assert(!cli::parse_arguments(
        static_cast<int>(invalid_interpreter_policy_arguments.size()),
        invalid_interpreter_policy_arguments.data(),
        invalid_interpreter_policy_diagnostics));

    const auto incremental_systemc_source = directory / "incremental.cpp";
    const auto systemc_object = directory / "incremental.fsimscobj";
    const auto systemc_plugin = directory / "incremental.fsimscplugin";
    const auto systemc_design = directory / "incremental.fsimdesign";
    const auto systemc_source_text = support::path_to_utf8(incremental_systemc_source);
    const auto systemc_object_text = support::path_to_utf8(systemc_object);
    const auto systemc_plugin_text = support::path_to_utf8(systemc_plugin);
    const std::vector<const char*> systemc_compile_arguments {
        "fsim",
        "systemc",
        "compile",
        "--output",
        systemc_object_text.c_str(),
        "--define",
        "FSIM_TEST_WIDTH=8",
        "--compile-option",
        "-fno-omit-frame-pointer",
        systemc_source_text.c_str()
    };
    diagnostic::Engine systemc_compile_diagnostics;
    const auto systemc_compile = cli::parse_arguments(
        static_cast<int>(systemc_compile_arguments.size()),
        systemc_compile_arguments.data(), systemc_compile_diagnostics);
    assert(systemc_compile && !systemc_compile_diagnostics.has_error());
    assert(systemc_compile->command == cli::Command::systemc_compile);
    assert(systemc_compile->files == std::vector { incremental_systemc_source });
    assert(systemc_compile->artifact_output == systemc_object);
    assert(systemc_compile->defines
        == std::vector<std::string> { "FSIM_TEST_WIDTH=8" });
    assert(systemc_compile->systemc_compile_options == std::vector<std::string> { "-fno-omit-frame-pointer" });

    const std::vector<const char*> systemc_link_arguments {
        "fsim",
        "systemc",
        "link",
        "--object",
        systemc_object_text.c_str(),
        "--library",
        "vendor",
#if !defined(FSIM_TEST_ASAN_ENABLED)
        "--link-option",
        "-Wl,--no-undefined",
#endif
#if defined(FSIM_TEST_ASAN_ENABLED)
        "--link-option",
        "-fsanitize=address,undefined",
#endif
        "--output",
        systemc_plugin_text.c_str()
    };
    diagnostic::Engine systemc_link_diagnostics;
    const auto systemc_link = cli::parse_arguments(
        static_cast<int>(systemc_link_arguments.size()),
        systemc_link_arguments.data(), systemc_link_diagnostics);
    assert(systemc_link && !systemc_link_diagnostics.has_error());
    assert(systemc_link->command == cli::Command::systemc_link);
    assert(systemc_link->objects == std::vector { systemc_object });
    assert(systemc_link->library == "vendor");
    assert(systemc_link->artifact_output == systemc_plugin);

    const std::vector<const char*> systemc_elaborate_arguments {
        "fsim",
        "elaborate",
        "--object",
        object_text.c_str(),
        "--systemc-plugin",
        systemc_plugin_text.c_str(),
        "--top",
        "systemc:vendor.first",
        "--output",
        design_text.c_str()
    };
    diagnostic::Engine systemc_elaborate_diagnostics;
    const auto systemc_elaborate = cli::parse_arguments(
        static_cast<int>(systemc_elaborate_arguments.size()),
        systemc_elaborate_arguments.data(), systemc_elaborate_diagnostics);
    assert(systemc_elaborate && !systemc_elaborate_diagnostics.has_error());
    assert(systemc_elaborate->systemc_plugins == std::vector { systemc_plugin });

    const std::vector<const char*> project_arguments {
        "fsim", "simulate", "--project",
        "fsim.toml", "--design", design_text.c_str()
    };
    diagnostic::Engine project_diagnostics;
    assert(!cli::parse_arguments(static_cast<int>(project_arguments.size()),
        project_arguments.data(), project_diagnostics));

    bool compile_called = false;
    cli::Services services;
    const std::vector<const char*> vhdl_standard_arguments {
        "fsim", "check", "--lang", "vhdl-93",
        "--standard", "93", source_text.c_str()
    };
    const std::vector<const char*> vhdl_2019_standard_arguments {
        "fsim", "check", "--lang", "vhdl-2019",
        "--standard", "19", source_text.c_str()
    };
    const std::vector<const char*> verilog_standard_arguments {
        "fsim",
        "check",
        "--lang",
        "verilog-2001-noconfig",
        "--standard",
        "v2001-noconfig",
        source_text.c_str()
    };
    const std::vector<const char*> systemverilog_standard_arguments {
        "fsim", "check", "--lang", "sv-2009",
        "--standard", "09", source_text.c_str()
    };
    int standard_calls = 0;
    services.check = [&](const cli::Invocation& invocation,
                         const project::Config& config, diagnostic::Engine&,
                         std::ostream&, std::ostream&) {
        assert(config.source_sets.size() == 1);
        if (standard_calls == 0) {
            assert(invocation.language == project::Language::vhdl);
            assert(invocation.standard == "93");
            assert(config.source_sets.front().standard == "1993");
        } else if (standard_calls == 1) {
            assert(invocation.language == project::Language::vhdl);
            assert(invocation.standard == "19");
            assert(config.source_sets.front().standard == "2019");
        } else if (standard_calls == 2) {
            assert(invocation.language == project::Language::verilog);
            assert(invocation.standard == "v2001-noconfig");
            assert(config.source_sets.front().standard == "2001-noconfig");
        } else {
            assert(invocation.language == project::Language::system_verilog);
            assert(invocation.standard == "09");
            assert(config.source_sets.front().standard == "2009");
        }
        ++standard_calls;
        return 0;
    };
    services.compile = [&](const cli::Invocation& invocation,
                           const project::Config& config, diagnostic::Engine&,
                           std::ostream&, std::ostream&) {
        compile_called = true;
        assert(invocation.command == cli::Command::compile);
        assert(config.source_sets.size() == 1);
        assert(config.source_sets.front().files == std::vector { source });
        return 0;
    };
    std::ostringstream output;
    std::ostringstream error;
    assert(cli::run(static_cast<int>(vhdl_standard_arguments.size()),
               vhdl_standard_arguments.data(), services, output,
               error)
        == 0);
    output.str({ });
    error.str({ });
    assert(cli::run(static_cast<int>(vhdl_2019_standard_arguments.size()),
               vhdl_2019_standard_arguments.data(), services, output,
               error)
        == 0);
    output.str({ });
    error.str({ });
    assert(cli::run(static_cast<int>(verilog_standard_arguments.size()),
               verilog_standard_arguments.data(), services, output,
               error)
        == 0);
    output.str({ });
    error.str({ });
    assert(cli::run(static_cast<int>(systemverilog_standard_arguments.size()),
               systemverilog_standard_arguments.data(), services, output,
               error)
        == 0);
    assert(standard_calls == 4);
    assert(error.str().empty());
    const std::vector<const char*> help_arguments { "fsim", "--help" };
    output.str({ });
    error.str({ });
    assert(cli::run(static_cast<int>(help_arguments.size()),
               help_arguments.data(), services, output, error)
        == 0);
    assert(output.str().find("Verilog: 95/1995") != std::string::npos);
    assert(output.str().find("19/2019") != std::string::npos);
    assert(output.str().find("SystemVerilog: 05/2005") != std::string::npos);
    assert(error.str().empty());
    output.str({ });
    error.str({ });
    assert(cli::run(static_cast<int>(compile_arguments.size()),
               compile_arguments.data(), services, output, error)
        == 0);
    assert(compile_called);
    assert(error.str().empty());

    bool elaborate_called = false;
    services.elaborate = [&](const cli::Invocation& invocation,
                             const project::Config& config, diagnostic::Engine&,
                             std::ostream&, std::ostream&) {
        elaborate_called = true;
        assert(invocation.command == cli::Command::elaborate);
        assert(config.manifest_path == "<non-project>");
        assert(config.project.tops == invocation.tops);
        return 0;
    };
    output.str({ });
    error.str({ });
    assert(cli::run(static_cast<int>(elaborate_arguments.size()),
               elaborate_arguments.data(), services, output, error)
        == 0);
    assert(elaborate_called);
    assert(error.str().empty());

    output.str({ });
    error.str({ });
    auto production_services = app::make_cli_services();

    {
        std::ofstream systemc_output(incremental_systemc_source);
        systemc_output << R"(#include "fsim/systemc.hpp"
SC_MODULE(IncrementalTop) {
  sc_core::sc_signal<sc_dt::sc_uint<8>> value{"value"};
  SC_CTOR(IncrementalTop) {
    SC_METHOD(initialize);
  }
  void initialize() {
    value.write(sc_dt::sc_uint<8>{5});
  }
};
SC_FSIM_EXPORT_AS(IncrementalTop, "first");
)";
    }
    output.str({ });
    error.str({ });
    const auto systemc_compile_status = cli::run(
        static_cast<int>(systemc_compile_arguments.size()),
        systemc_compile_arguments.data(), production_services, output, error);
    if (systemc_compile_status != 0) {
        std::cerr << error.str();
    }
    assert(systemc_compile_status == 0);
    assert(error.str().empty());
    output.str({ });
    error.str({ });
    const auto systemc_link_status = cli::run(
        static_cast<int>(systemc_link_arguments.size()),
        systemc_link_arguments.data(), production_services, output, error);
    if (systemc_link_status != 0) {
        std::cerr << error.str();
    }
    assert(systemc_link_status == 0);
    assert(error.str().empty());
    diagnostic::Engine systemc_object_inspection_diagnostics;
    const auto systemc_object_inspection = app::inspect_artifact(
        systemc_object, systemc_object_inspection_diagnostics);
    assert(systemc_object_inspection);
    assert(!systemc_object_inspection_diagnostics.has_error());
    assert(systemc_object_inspection->phase == app::ArtifactPhaseKind::systemc_compilation);
    assert(systemc_object_inspection->language == "systemc");
    assert(systemc_object_inspection->toolchain.has_value());
    assert(systemc_object_inspection->target.has_value());
    diagnostic::Engine systemc_plugin_inspection_diagnostics;
    const auto systemc_plugin_inspection = app::inspect_artifact(
        systemc_plugin, systemc_plugin_inspection_diagnostics);
    assert(systemc_plugin_inspection);
    assert(!systemc_plugin_inspection_diagnostics.has_error());
    assert(systemc_plugin_inspection->phase == app::ArtifactPhaseKind::systemc_link);
    assert(systemc_plugin_inspection->library == "vendor");
    assert(systemc_plugin_inspection->units == std::vector<std::string> { "systemc:vendor.first" });
    project::Config systemc_phase_config;
    systemc_phase_config.base_directory = directory;
    systemc_phase_config.project.name = "systemc-phase";
    systemc_phase_config.project.top = "systemc:vendor.first";
    systemc_phase_config.build.cache_path = directory / "systemc-phase-cache";
    const std::vector<std::filesystem::path> no_hdl_objects;
    const std::vector systemc_plugin_inputs { systemc_plugin };
    diagnostic::Engine systemc_publish_diagnostics;
    const auto systemc_published = app::elaborate_artifact(
        systemc_phase_config, no_hdl_objects, systemc_plugin_inputs,
        systemc_design, systemc_publish_diagnostics);
    if (!systemc_published) {
        diagnostic::print_text(std::cerr, systemc_publish_diagnostics);
    }
    assert(systemc_published);
    assert(!systemc_publish_diagnostics.has_error());

    diagnostic::Engine systemc_design_metadata_diagnostics;
    const auto systemc_design_metadata = artifact::load_design_metadata(
        systemc_design, systemc_design_metadata_diagnostics);
    assert(systemc_design_metadata);
    assert(!systemc_design_metadata_diagnostics.has_error());
    assert(systemc_design_metadata->format == artifact::kDesignFormatVersion);
    assert(systemc_design_metadata->objects.empty());
    assert(systemc_design_metadata->systemc_plugins.size() == 1);
    assert(systemc_design_metadata->systemc_plugins.front().logical_library == "vendor");
    assert(systemc_design_metadata->systemc_plugins.front().factories == std::vector<std::string> { "first" });
    assert(systemc_design_metadata->systemc_plugins.front().scv_compatibility
        == fsim_scv_compatibility_identity());

    const auto relocated_systemc_design = directory
        / support::path_from_utf8("relocated-\xc2\xb5.fsimdesign");
    copy_tree(systemc_design, relocated_systemc_design);
    diagnostic::Engine relocated_systemc_diagnostics;
    const auto relocated_systemc = app::load_design_artifact(
        relocated_systemc_design, relocated_systemc_diagnostics);
    assert(relocated_systemc && !relocated_systemc_diagnostics.has_error());
    assert(relocated_systemc->systemc_plugins.size() == 1);

    const auto stale_scv_design = directory / "stale-scv.fsimdesign";
    copy_tree(systemc_design, stale_scv_design);
    make_tree_writable(stale_scv_design);
    auto stale_scv_metadata = *systemc_design_metadata;
    auto& stale_scv_identity =
        stale_scv_metadata.systemc_plugins.front().scv_compatibility;
    const auto stdlib = stale_scv_identity.find("|stdlib=");
    assert(stdlib != std::string::npos);
    stale_scv_identity[stdlib + 8U] =
        stale_scv_identity[stdlib + 8U] == 'x' ? 'y' : 'x';
    stale_scv_metadata.design_digest =
        artifact::compute_design_digest(stale_scv_metadata);
    {
        const auto bytes = artifact::serialize_design_metadata(stale_scv_metadata);
        std::ofstream design_metadata_output(
            stale_scv_design / artifact::kDesignMetadataFilename,
            std::ios::binary | std::ios::trunc);
        design_metadata_output.write(
            bytes.data(), static_cast<std::streamsize>(bytes.size()));
        assert(design_metadata_output);
    }
    diagnostic::Engine stale_scv_diagnostics;
    assert(!app::load_design_artifact(
        stale_scv_design, stale_scv_diagnostics));
    assert(std::ranges::any_of(
        stale_scv_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SCV-A001"
                && diagnostic.message.find("standard library mismatch")
                    != std::string::npos;
        }));

    const auto stale_systemc_design = directory / "stale-incremental.fsimdesign";
    copy_tree(systemc_design, stale_systemc_design);
    make_tree_writable(stale_systemc_design);
    auto stale_design_metadata = *systemc_design_metadata;
    auto& stale_design_plugin = stale_design_metadata.systemc_plugins.front();
    const auto stale_plugin_directory = stale_systemc_design / stale_design_plugin.directory;
    diagnostic::Engine stale_plugin_metadata_diagnostics;
    auto stale_plugin_metadata = systemc::load_incremental_plugin_metadata(
        stale_plugin_directory, stale_plugin_metadata_diagnostics);
    assert(stale_plugin_metadata);
    assert(!stale_plugin_metadata_diagnostics.has_error());
    stale_plugin_metadata->compiler_fingerprint = std::string(64, 'c');
    stale_plugin_metadata->input_digest = systemc::compute_incremental_plugin_input_digest(*stale_plugin_metadata);
    stale_plugin_metadata->link_digest = systemc::compute_incremental_plugin_digest(*stale_plugin_metadata);
    const auto stale_plugin_metadata_bytes = systemc::serialize_incremental_plugin_metadata(*stale_plugin_metadata);
    {
        std::ofstream plugin_metadata_output(
            stale_plugin_directory / systemc::kIncrementalPluginMetadataFilename,
            std::ios::binary | std::ios::trunc);
        plugin_metadata_output.write(
            stale_plugin_metadata_bytes.data(),
            static_cast<std::streamsize>(stale_plugin_metadata_bytes.size()));
        assert(plugin_metadata_output);
    }
    stale_design_plugin.compiler_fingerprint = stale_plugin_metadata->compiler_fingerprint;
    stale_design_plugin.input_digest = stale_plugin_metadata->input_digest;
    stale_design_plugin.link_digest = stale_plugin_metadata->link_digest;
    stale_design_plugin.metadata_checksum = support::Sha256::hex(
        support::Sha256::digest(stale_plugin_metadata_bytes));
    const auto stale_metadata_artifact = stale_design_plugin.directory
        / systemc::kIncrementalPluginMetadataFilename;
    const auto stale_metadata_payload = std::ranges::find_if(
        stale_design_metadata.payloads, [&](const auto& payload) {
            return payload.artifact == stale_metadata_artifact;
        });
    assert(stale_metadata_payload != stale_design_metadata.payloads.end());
    stale_metadata_payload->checksum = stale_design_plugin.metadata_checksum;
    stale_design_metadata.design_digest = artifact::compute_design_digest(stale_design_metadata);
    {
        const auto bytes = artifact::serialize_design_metadata(stale_design_metadata);
        std::ofstream design_metadata_output(
            stale_systemc_design / artifact::kDesignMetadataFilename,
            std::ios::binary | std::ios::trunc);
        design_metadata_output.write(
            bytes.data(), static_cast<std::streamsize>(bytes.size()));
        assert(design_metadata_output);
    }
    diagnostic::Engine stale_systemc_design_diagnostics;
    assert(!app::load_design_artifact(
        stale_systemc_design, stale_systemc_design_diagnostics));
    const auto stale_producer_rejected = std::ranges::any_of(
        stale_systemc_design_diagnostics.diagnostics(),
        [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ART-0014"
                && diagnostic.message.find("producer identity is stale")
                != std::string::npos;
        });
    if (!stale_producer_rejected) {
        diagnostic::print_text(std::cerr, stale_systemc_design_diagnostics);
    }
    assert(stale_producer_rejected);

    const auto hidden_systemc_source = directory / "incremental.cpp.producer-hidden";
    const auto hidden_systemc_object = directory / "incremental.fsimscobj.producer-hidden";
    const auto systemc_plugin_metadata = systemc_plugin / systemc::kIncrementalPluginMetadataFilename;
    const auto hidden_systemc_plugin_metadata = systemc_plugin / (std::string { systemc::kIncrementalPluginMetadataFilename } + ".producer-hidden");
    const auto rename_producer = [&](const std::filesystem::path& producer_path,
                                     const std::filesystem::path& destination,
                                     const std::string_view label) {
        std::error_code operation_error;
        std::filesystem::rename(producer_path, destination, operation_error);
        if (operation_error) {
            std::cerr << "non-project producer hiding: " << label << ": "
                      << operation_error.value() << " (" << operation_error.message()
                      << ")\n";
        }
        assert(!operation_error);
        std::cerr << "non-project producer hiding: " << label << " complete\n";
    };
    const auto make_writable = [&](const std::filesystem::path& path,
                                   const std::string_view label) {
        std::error_code operation_error;
        std::filesystem::permissions(path, std::filesystem::perms::owner_write,
            std::filesystem::perm_options::add,
            operation_error);
        if (operation_error) {
            std::cerr << "non-project producer hiding: " << label << ": "
                      << operation_error.value() << " (" << operation_error.message()
                      << ")\n";
        }
        assert(!operation_error);
        std::cerr << "non-project producer hiding: " << label << " complete\n";
    };
    rename_producer(incremental_systemc_source, hidden_systemc_source,
        "source rename");
    rename_producer(systemc_object, hidden_systemc_object, "object rename");
    // Windows locks a loaded DLL against renaming its containing artifact.
    // Hiding the required metadata makes the producer artifact unusable while
    // leaving the loaded native image at its stable path. Publication makes
    // both the artifact and its contents read-only, so restore write access only
    // to the directory and metadata file being renamed.
    make_writable(systemc_plugin, "artifact permissions");
    make_writable(systemc_plugin_metadata, "metadata permissions");
    rename_producer(systemc_plugin_metadata, hidden_systemc_plugin_metadata,
        "metadata rename");
    std::cerr << "non-project producer hiding: loading embedded design\n";
    diagnostic::Engine systemc_design_load_diagnostics;
    auto loaded_systemc_design = app::load_design_artifact(
        systemc_design, systemc_design_load_diagnostics);
    if (!loaded_systemc_design) {
        diagnostic::print_text(std::cerr, systemc_design_load_diagnostics);
    }
    assert(loaded_systemc_design && !systemc_design_load_diagnostics.has_error());
    std::cerr << "non-project producer hiding: embedded design loaded\n";
    assert(loaded_systemc_design->systemc_hierarchies.size() == 1);
    assert(loaded_systemc_design->systemc_roots.size() == 1);
    assert(loaded_systemc_design->systemc_plugins.size() == 1);
    assert(loaded_systemc_design->design.systemc_processes().size() == 1);
    assert(loaded_systemc_design->design.systemc_instances().size() == 1);
    assert(loaded_systemc_design->design.systemc_instances()
               .front()
               .internal_signals.size()
        == 1);
    std::cerr << "non-project producer hiding: embedded design validated\n";
    const auto systemc_value = loaded_systemc_design->design.systemc_instances()
                                   .front()
                                   .internal_signals.front()
                                   .signal;
    app::Simulation systemc_standalone_simulation {
        std::move(*loaded_systemc_design), 1000,
        app::SimulationEngine::interpreter
    };
    const auto systemc_standalone_result = systemc_standalone_simulation.run();
    std::cerr << "non-project producer hiding: embedded simulation completed\n";
    const auto systemc_standalone_value = systemc_standalone_simulation.read_signal(systemc_value).to_msb_string();
    std::cerr << "non-project producer hiding: simulation status="
              << static_cast<int>(systemc_standalone_result.status)
              << " callbacks=" << systemc_standalone_result.callbacks_executed
              << " value=" << systemc_standalone_value << '\n';
    assert(systemc_standalone_result.status == runtime::RunStatus::completed);
    assert(systemc_standalone_result.callbacks_executed != 0);
    assert(systemc_standalone_value == "00000101");
    std::cerr << "non-project producer hiding: embedded simulation validated\n";

    assert(cli::run(static_cast<int>(compile_arguments.size()),
               compile_arguments.data(), production_services, output,
               error)
        == 0);
    assert(error.str().empty());
    assert(output.str().find("compiled 1 source file(s)") != std::string::npos);

    diagnostic::Engine object_diagnostics;
    const auto metadata = artifact::load_object_metadata(object, object_diagnostics);
    assert(metadata && !object_diagnostics.has_error());
    assert(metadata->language == "systemverilog");
    assert(metadata->standard == "2017");
    assert(metadata->library == "work");
    assert(metadata->compilation_unit == "source-set");
    assert(metadata->sources.size() == 1);
    assert(!metadata->units.empty());
    const auto object_trace = app::decode_trace_archive(
        app::trace_archive_from_hex(metadata->trace_archive),
        app::TraceArchiveKind::Object);
    assert(object_trace.ok()
        && object_trace.snapshot.output_intent == "phase.fst"
        && !object_trace.snapshot.output_intent.is_absolute());
    assert(std::ranges::any_of(metadata->units, [](const auto& indexed) {
        return indexed.name == "tb";
    }));
    std::cerr << "non-project cli: HDL object metadata validated\n";

    const auto& indexed_unit = metadata->units.front();
    std::ifstream unit_input(object / indexed_unit.artifact, std::ios::binary);
    assert(unit_input);
    const std::string unit_bytes { std::istreambuf_iterator<char> { unit_input },
        std::istreambuf_iterator<char> { } };
    assert(!unit_input.bad());
    unit_input.close();
    assert(!unit_input.is_open());
    diagnostic::Engine unit_diagnostics;
    const auto unit = library::deserialize_portable_unit(
        unit_bytes, support::path_to_utf8(indexed_unit.artifact),
        unit_diagnostics);
    assert(unit && !unit_diagnostics.has_error());
    assert(unit->name == indexed_unit.name);
    assert(!unit->span.source_name.empty());
    assert(!support::path_from_utf8(unit->span.source_name.str()).is_absolute());
    std::cerr << "non-project cli: portable unit validated\n";

    output.str({ });
    error.str({ });
    assert(cli::run(static_cast<int>(extra_compile_arguments.size()),
               extra_compile_arguments.data(), production_services, output,
               error)
        == 0);
    assert(error.str().empty());

    const auto hidden_source = directory / "tb.sv.producer-hidden";
    const auto hidden_extra_source = directory / "extra.sv.producer-hidden";
    std::filesystem::rename(source, hidden_source);
    std::filesystem::rename(extra_source, hidden_extra_source);
    diagnostic::Engine load_diagnostics;
    const std::vector object_inputs { object, extra_object };
    const auto loaded = app::load_objects(object_inputs, load_diagnostics);
    assert(loaded && !load_diagnostics.has_error());
    assert(loaded->objects.size() == 2);
    assert(loaded->parsed.units.size() == 3);
    assert(loaded->parsed.units[0].name == "child");
    assert(loaded->parsed.units[1].name == "tb");
    assert(loaded->parsed.units[2].name == "extra");
    assert(loaded->semantics.valid());
    std::cerr << "non-project cli: relocated objects loaded\n";
    project::Config object_config;
    object_config.base_directory = directory;
    object_config.project.name = "non-project-object-build";
    object_config.project.tops.push_back({ "sv:work.tb", "primary" });
    object_config.project.time_resolution = "1ns";
    object_config.build.cache_path = directory / "object-build-cache";
    diagnostic::Engine object_build_diagnostics;
    const auto object_build = app::build_objects(object_config, object_inputs,
        object_build_diagnostics);
    assert(object_build && !object_build_diagnostics.has_error());
    assert(object_build->design.roots() == std::vector<std::string> { "primary" });
    assert(!object_build->design.processes().empty());
    assert(object_build->design_ir.valid(object_build->semantics));
    const auto object_provenance = app::verilog_scope_provenance(*object_build);
    assert(!object_provenance.empty());
    assert(std::ranges::all_of(object_provenance, [](const auto& provenance) {
        return provenance.language == semantic::Language::system_verilog && provenance.standard == "systemverilog-2017" && provenance.compatibility_profile == "none" && !std::filesystem::path(provenance.source_path).is_absolute();
    }));
    assert(std::ranges::any_of(object_provenance, [](const auto& provenance) {
        return provenance.path == "primary" && provenance.semantic_unit == "work::tb";
    }));
    const auto provenance_signature = [](const auto& provenance) {
        std::vector<std::string> result;
        result.reserve(provenance.size());
        for (const auto& item : provenance) {
            result.push_back(item.path + "|" + item.semantic_unit + "|" + std::to_string(item.unit.value()) + "|" + std::to_string(item.source.value()) + "|" + item.source_path + "|" + item.standard + "|" + item.compatibility_profile);
        }
        return result;
    };
    const auto object_provenance_signature = provenance_signature(object_provenance);
    diagnostic::Engine state_diagnostics;
    const auto runtime_state = app::serialize_runtime_state(object_build->design, state_diagnostics);
    const auto semantic_state = app::serialize_semantic_state(object_build->semantics, state_diagnostics);
    const auto design_ir_state = app::serialize_design_ir_state(
        object_build->design_ir, state_diagnostics);
    assert(runtime_state && semantic_state && design_ir_state);
    assert(!state_diagnostics.has_error());
    diagnostic::Engine restore_diagnostics;
    const auto restored_runtime = app::deserialize_runtime_state(
        *runtime_state, "runtime", restore_diagnostics);
    const auto restored_semantics = app::deserialize_semantic_state(
        *semantic_state, "semantics", restore_diagnostics);
    const auto restored_design_ir = app::deserialize_design_ir_state(
        *design_ir_state, "design-ir", restore_diagnostics);
    assert(restored_runtime && restored_semantics && restored_design_ir);
    assert(!restore_diagnostics.has_error());
    assert(restored_runtime->signal_paths() == object_build->design.signal_paths());
    assert(restored_runtime->processes().size() == object_build->design.processes().size());
    assert(restored_design_ir->valid(*restored_semantics));
    diagnostic::Engine deterministic_state_diagnostics;
    assert(app::serialize_runtime_state(*restored_runtime,
               deterministic_state_diagnostics)
        == runtime_state);
    assert(app::serialize_semantic_state(*restored_semantics,
               deterministic_state_diagnostics)
        == semantic_state);
    assert(app::serialize_design_ir_state(*restored_design_ir,
               deterministic_state_diagnostics)
        == design_ir_state);
    std::cerr << "non-project cli: state round trip validated\n";

    const std::vector<const char*> production_elaborate_arguments {
        "fsim", "elaborate",
        "--object", object_text.c_str(),
        "--object", extra_object_text.c_str(),
        "--top", "primary=sv:work.tb",
        "--output", design_text.c_str(),
        "--delay-mode", "typ",
        "--seed", "9",
        "--optimization", "O0"
    };
    output.str({ });
    error.str({ });
    assert(cli::run(static_cast<int>(production_elaborate_arguments.size()),
               production_elaborate_arguments.data(), production_services,
               output, error)
        == 0);
    assert(error.str().empty());
    assert(output.str().find("elaborated 1 root(s)") != std::string::npos);
#if defined(FSIM_HAS_LLVM)
    const std::vector<const char*> aot_elaborate_arguments {
        "fsim", "elaborate",
        "--object", object_text.c_str(),
        "--object", extra_object_text.c_str(),
        "--top", "primary=sv:work.tb",
        "--output", aot_design_text.c_str(),
        "--delay-mode", "typ", "--seed", "9",
        "--optimization", "O0",
        "--aot", "--aot-scope", "all",
        "--cache", consumer_cache_text.c_str()
    };
    output.str({ });
    error.str({ });
    const auto aot_status = cli::run(
        static_cast<int>(aot_elaborate_arguments.size()),
        aot_elaborate_arguments.data(), production_services, output, error);
    if (aot_status != 0) {
        std::cerr << error.str();
    }
    assert(aot_status == 0);
    assert(error.str().empty());
    assert(output.str().find("scope=all") != std::string::npos);
    assert(std::filesystem::is_directory(
        consumer_cache / "llvm-native" / "aot-receipts"));
    std::optional<std::filesystem::path> receipt_path;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             consumer_cache / "llvm-native" / "aot-receipts")) {
        if (entry.is_regular_file() && entry.path().extension() == ".fobj") {
            receipt_path = entry.path();
            break;
        }
    }
    assert(receipt_path.has_value());
    {
        std::ofstream corrupt_receipt(
            *receipt_path, std::ios::binary | std::ios::trunc);
        corrupt_receipt << "corrupt receipt";
        assert(corrupt_receipt.good());
    }
    const std::vector<const char*> receipt_probe_arguments {
        "fsim", "simulate", "--design", design_text.c_str(),
        "--engine", "compiled", "--optimization", "O0",
        "--duration", "1ns", "--cache", consumer_cache_text.c_str(),
        "--trace-lifecycle", "disabled"
    };
    output.str({ });
    error.str({ });
    assert(cli::run(static_cast<int>(receipt_probe_arguments.size()),
               receipt_probe_arguments.data(), production_services,
               output, error)
        == 0);
    assert(error.str().find("FSIM-AOT-002") != std::string::npos);
    assert(output.str().find("using forced-all AOT cache receipt")
        == std::string::npos);

    const std::vector<const char*> selected_aot_arguments {
        "fsim", "elaborate",
        "--object", object_text.c_str(),
        "--object", extra_object_text.c_str(),
        "--top", "primary=sv:work.tb",
        "--output", selected_aot_design_text.c_str(),
        "--delay-mode", "typ", "--seed", "9",
        "--optimization", "O0",
        "--aot", "--aot-scope", "selected",
        "--cache", consumer_cache_text.c_str()
    };
    output.str({ });
    error.str({ });
    assert(cli::run(static_cast<int>(selected_aot_arguments.size()),
               selected_aot_arguments.data(), production_services,
               output, error)
        == 0);
    assert(error.str().empty());
    assert(output.str().find("scope=selected") != std::string::npos);
    output.str({ });
    error.str({ });
    assert(cli::run(static_cast<int>(receipt_probe_arguments.size()),
               receipt_probe_arguments.data(), production_services,
               output, error)
        == 0);
    assert(error.str().empty());
    assert(output.str().find("using forced-all AOT cache receipt")
        == std::string::npos);

    output.str({ });
    error.str({ });
    const std::vector<const char*> final_aot_arguments {
        "fsim", "elaborate",
        "--object", object_text.c_str(),
        "--object", extra_object_text.c_str(),
        "--top", "primary=sv:work.tb",
        "--output", final_aot_design_text.c_str(),
        "--delay-mode", "typ", "--seed", "9",
        "--optimization", "O0",
        "--aot", "--aot-scope", "all",
        "--cache", consumer_cache_text.c_str()
    };
    assert(cli::run(static_cast<int>(final_aot_arguments.size()),
               final_aot_arguments.data(), production_services,
               output, error)
        == 0);
    assert(error.str().empty());
    assert(output.str().find("scope=all") != std::string::npos);
    const std::vector<const char*> selected_override_arguments {
        "fsim", "simulate", "--design", design_text.c_str(),
        "--engine", "compiled", "--compiled-processes", "selected",
        "--optimization", "O0", "--duration", "1ns",
        "--cache", consumer_cache_text.c_str(),
        "--trace-lifecycle", "disabled"
    };
    output.str({ });
    error.str({ });
    assert(cli::run(static_cast<int>(selected_override_arguments.size()),
               selected_override_arguments.data(), production_services,
               output, error)
        == 0);
    assert(error.str().empty());
    assert(output.str().find("using forced-all AOT cache receipt")
        == std::string::npos);
    std::optional<std::filesystem::path> removed_native_object;
    const auto native_objects
        = consumer_cache / "llvm-native" / "llvm" / "objects";
    for (const auto& entry :
        std::filesystem::recursive_directory_iterator(native_objects)) {
        if (entry.is_regular_file() && entry.path().extension() == ".fobj") {
            removed_native_object = entry.path();
            assert(std::filesystem::remove(*removed_native_object));
            break;
        }
    }
    assert(removed_native_object.has_value());
#endif
    diagnostic::Engine design_metadata_diagnostics;
    const auto published_design_metadata = artifact::load_design_metadata(design, design_metadata_diagnostics);
    assert(published_design_metadata && !design_metadata_diagnostics.has_error());
    assert(published_design_metadata->objects.size() == 2);
    const auto design_trace = app::decode_trace_archive(
        app::trace_archive_from_hex(published_design_metadata->trace_archive),
        app::TraceArchiveKind::Design);
    assert(design_trace.ok()
        && design_trace.snapshot.profile_identity
            == object_trace.snapshot.profile_identity
        && design_trace.snapshot.declaration_identity
            == object_trace.snapshot.declaration_identity
        && design_trace.snapshot.output_intent
            == object_trace.snapshot.output_intent);
    assert(published_design_metadata->roots.front().selected_identity == "sv:work.tb");
    std::cerr << "non-project cli: HDL design published\n";

    const auto sdf_base_design = directory / "sdf-base.fsimdesign";
    const auto sdf_design = directory / "sdf-state.fsimdesign";
    const auto sdf_source = directory / "timing.sdf";
    const std::string sdf_text = "(DELAYFILE\n"
                                 "  (SDFVERSION \"4.0\")\n"
                                 "  (DESIGN \"tb\")\n"
                                 "  (VENDOR \"fsim\")\n"
                                 "  (DIVIDER .)\n"
                                 "  (TIMESCALE 1 ns)\n"
                                 "  (CELL (CELLTYPE \"tb\") (INSTANCE primary)\n"
                                 "    (DELAY (ABSOLUTE (INTERCONNECT q child_y (1)))))\n"
                                 ")";
    {
        std::ofstream sdf_output(sdf_source, std::ios::binary);
        sdf_output << sdf_text;
        assert(sdf_output.good());
    }
    diagnostic::Engine sdf_build_diagnostics;
    auto sdf_build = app::build_objects(object_config, object_inputs, sdf_build_diagnostics);
    assert(sdf_build && !sdf_build_diagnostics.has_error());
    diagnostic::Engine sdf_base_diagnostics;
    assert(app::publish_design_artifact(object_config, *sdf_build,
        sdf_base_design, sdf_base_diagnostics));
    assert(!sdf_base_diagnostics.has_error());
    diagnostic::Engine sdf_base_metadata_diagnostics;
    const auto sdf_base_metadata = artifact::load_design_metadata(
        sdf_base_design, sdf_base_metadata_diagnostics);
    assert(sdf_base_metadata && !sdf_base_metadata_diagnostics.has_error());
    auto parsed_sdf = frontend::parse_sdf({ support::path_to_utf8(sdf_source), sdf_text });
    assert(parsed_sdf.ok());
    app::SdfAnnotationScopeRequest sdf_scope_request;
    sdf_scope_request.selection = app::SdfScopeSelection::All;
    sdf_scope_request.expected_project_identity = "non-project-sdf";
    sdf_scope_request.expected_design_identity = sdf_build->cache_key;
    const auto sdf_scope = app::bind_sdf_annotation_scope(
        parsed_sdf.file, sdf_build->design, "non-project-sdf",
        sdf_build->cache_key, sdf_scope_request);
    assert(sdf_scope.ok());
    const auto sdf_cells = app::resolve_sdf_cells(sdf_scope.scope, sdf_build->design);
    assert(sdf_cells.ok());
    const auto sdf_endpoints = app::resolve_sdf_endpoints(sdf_cells.resolution, sdf_build->design);
    if (!sdf_endpoints.ok()) {
        for (const auto& diagnostic : sdf_endpoints.diagnostics)
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        for (const auto& [path, signal] : sdf_build->design.signal_paths()) {
            (void)signal;
            std::cerr << "SDF candidate signal: " << path << '\n';
        }
    }
    assert(sdf_endpoints.ok());
    const auto sdf_mapping = app::validate_sdf_mapping(sdf_endpoints.resolution, sdf_build->design);
    assert(sdf_mapping.ok());
    const app::SdfSchemaOptions sdf_options { "sdf-parse-options-v1",
        "sdf-normalization-options-v1",
        "fsim-compiler-compat-v1" };
    const auto sdf_schema = app::encode_sdf_schema(
        parsed_sdf.file, *sdf_mapping.summary, sdf_text, sdf_options);
    assert(sdf_schema.ok());
    const auto sdf_snapshot = app::decode_sdf_schema(sdf_schema.bytes, "fsim-compiler-compat-v1",
        sdf_mapping.summary->semantic_identity());
    assert(sdf_snapshot.ok());
    const auto sdf_identity = app::build_sdf_artifact_identity(
        *sdf_snapshot.snapshot, *sdf_mapping.summary,
        sdf_base_metadata->design_digest, app::SdfDelaySelectionPolicy::Typical);
    assert(sdf_identity.ok());
    const auto sdf_archive = app::encode_sdf_portable_archive(
        *sdf_snapshot.snapshot, *sdf_mapping.summary, *sdf_identity.annotation,
        sdf_schema.bytes);
    assert(sdf_archive.ok());
    diagnostic::Engine sdf_install_diagnostics;
    assert(app::install_sdf_phase_artifact(*sdf_build, sdf_archive.bytes,
        *sdf_identity.annotation,
        sdf_install_diagnostics));
    assert(!sdf_install_diagnostics.has_error());
    diagnostic::Engine sdf_publish_diagnostics;
    assert(app::publish_design_artifact(object_config, *sdf_build, sdf_design,
        sdf_publish_diagnostics));
    assert(!sdf_publish_diagnostics.has_error());
    std::filesystem::rename(sdf_source, directory / "timing.sdf.producer-hidden");
    diagnostic::Engine sdf_metadata_diagnostics;
    const auto sdf_metadata = artifact::load_design_metadata(sdf_design, sdf_metadata_diagnostics);
    assert(sdf_metadata && !sdf_metadata_diagnostics.has_error());
    assert(sdf_metadata->sdf_annotations.size() == 1);
    assert(sdf_metadata->cache_key != sdf_base_metadata->cache_key);
    assert(sdf_metadata->specialization_cache_keys != sdf_base_metadata->specialization_cache_keys);
    const auto sdf_cache = directory / "sdf-native-cache";
    const auto run_sdf_design = [&](const std::filesystem::path& path,
                                    const app::SimulationEngine engine) {
        diagnostic::Engine diagnostics;
        auto loaded_sdf = app::load_design_artifact(path, diagnostics);
        if (!loaded_sdf)
            diagnostic::print_text(std::cerr, diagnostics);
        assert(loaded_sdf && !diagnostics.has_error());
        assert(app::sdf_phase_artifacts(*loaded_sdf).size() == 1);
        assert(app::sdf_phase_artifacts(*loaded_sdf)
                   .front()
                   ->snapshot()
                   .annotation()
            == sdf_metadata->sdf_annotations.front());
        loaded_sdf->cache_path = sdf_cache;
        app::Simulation simulation { std::move(*loaded_sdf), 1000, engine };
        const auto cache = simulation.native_cache_statistics();
        const auto result = simulation.run();
        assert(result.status == runtime::RunStatus::stopped && result.time == 3);
        return cache;
    };
    run_sdf_design(sdf_design, app::SimulationEngine::interpreter);
    const auto sdf_cold = run_sdf_design(sdf_design, app::SimulationEngine::compiled);
    const auto sdf_warm = run_sdf_design(sdf_design, app::SimulationEngine::compiled);
    static_cast<void>(sdf_cold);
    static_cast<void>(sdf_warm);
#if defined(FSIM_HAS_LLVM)
    assert(sdf_cold.misses != 0 && sdf_cold.stores != 0);
    assert(sdf_warm.hits != 0);
#endif
    const auto relocated_sdf_design = directory / "relocated-sdf.fsimdesign";
    copy_tree(sdf_design, relocated_sdf_design);
    make_tree_writable(relocated_sdf_design);
    make_writable(sdf_design, "SDF design permissions");
    rename_producer(sdf_design,
        directory / "sdf-state.fsimdesign.producer-hidden",
        "SDF design relocation");
    const auto sdf_relocated = run_sdf_design(relocated_sdf_design, app::SimulationEngine::compiled);
    static_cast<void>(sdf_relocated);
#if defined(FSIM_HAS_LLVM)
    assert(sdf_relocated.hits != 0);
#endif
    const auto corrupt_sdf_design = directory / "corrupt-sdf.fsimdesign";
    copy_tree(relocated_sdf_design, corrupt_sdf_design);
    make_tree_writable(corrupt_sdf_design);
    const auto sdf_payload = std::ranges::find_if(
        sdf_metadata->payloads,
        [](const auto& payload) { return payload.kind.starts_with("sdf:"); });
    assert(sdf_payload != sdf_metadata->payloads.end());
    {
        std::fstream corrupt_output(
            corrupt_sdf_design / sdf_payload->artifact,
            std::ios::binary | std::ios::in | std::ios::out);
        assert(corrupt_output);
        char value { };
        corrupt_output.read(&value, 1);
        value = static_cast<char>(value ^ 1);
        corrupt_output.seekp(0);
        corrupt_output.write(&value, 1);
        assert(corrupt_output.good());
    }
    diagnostic::Engine corrupt_sdf_diagnostics;
    assert(
        !app::load_design_artifact(corrupt_sdf_design, corrupt_sdf_diagnostics));
    assert(corrupt_sdf_diagnostics.has_error());
    const auto partial_sdf_design = directory / "partial-sdf.fsimdesign";
    copy_tree(relocated_sdf_design, partial_sdf_design);
    make_tree_writable(partial_sdf_design);
    assert(std::filesystem::remove(partial_sdf_design / sdf_payload->artifact));
    diagnostic::Engine partial_sdf_diagnostics;
    assert(
        !app::load_design_artifact(partial_sdf_design, partial_sdf_diagnostics));
    assert(partial_sdf_diagnostics.has_error());
    const auto stale_sdf_design = directory / "stale-sdf.fsimdesign";
    copy_tree(relocated_sdf_design, stale_sdf_design);
    make_tree_writable(stale_sdf_design);
    auto stale_sdf_metadata = *sdf_metadata;
    stale_sdf_metadata.sdf_annotations.front().design_digest = std::string(64, '0');
    stale_sdf_metadata.design_digest = artifact::compute_design_digest(stale_sdf_metadata);
    {
        std::ofstream stale_output(
            stale_sdf_design / artifact::kDesignMetadataFilename,
            std::ios::binary | std::ios::trunc);
        stale_output << artifact::serialize_design_metadata(stale_sdf_metadata);
        assert(stale_output.good());
    }
    diagnostic::Engine stale_sdf_diagnostics;
    assert(!app::load_design_artifact(stale_sdf_design, stale_sdf_diagnostics));
    assert(std::ranges::any_of(
        stale_sdf_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SDF-PORTABLE-003";
        }));
    std::cerr << "non-project cli: SDF phase/cache persistence validated\n";

    const auto hidden_object = directory / "unit.fsimobj.producer-hidden";
    const auto hidden_extra_object = directory / "extra.fsimobj.producer-hidden";
    make_writable(object, "HDL object permissions");
    make_writable(extra_object, "extra HDL object permissions");
    rename_producer(object, hidden_object, "HDL object rename");
    rename_producer(extra_object, hidden_extra_object, "extra HDL object rename");
    std::cerr << "non-project cli: loading embedded HDL design\n";
    diagnostic::Engine design_load_diagnostics;
    auto loaded_design = app::load_design_artifact(design, design_load_diagnostics);
    if (!loaded_design) {
        diagnostic::print_text(std::cerr, design_load_diagnostics);
    }
    assert(loaded_design && !design_load_diagnostics.has_error());
    std::cerr << "non-project cli: embedded HDL design loaded\n";
    assert(loaded_design->design_ir.valid(loaded_design->semantics));
    assert(loaded_design->artifact_identity == published_design_metadata->design_digest);
    const auto loaded_provenance = app::verilog_scope_provenance(*loaded_design);
    assert(provenance_signature(loaded_provenance) == object_provenance_signature);
    app::Simulation standalone_simulation { std::move(*loaded_design), 1000,
        app::SimulationEngine::interpreter };
    assert(
        provenance_signature(standalone_simulation.verilog_scope_provenance()) == object_provenance_signature);
    const auto standalone_result = standalone_simulation.run();
    assert(standalone_result.status == runtime::RunStatus::stopped);
    assert(standalone_result.time == 3);
    std::cerr << "non-project cli: HDL standalone simulation validated\n";
    std::filesystem::create_directories(consumer_cache);
    std::filesystem::create_directories(consumer_file_root);
    const std::vector<const char*> conflict_arguments {
        "fsim", "simulate",
        "--design", design_text.c_str(),
        "--trace", conflict_trace_text.c_str(),
        "--trace-format", "vcd",
        "--trace-select", "primary.*"
    };
    output.str({ });
    error.str({ });
    assert(cli::run(static_cast<int>(conflict_arguments.size()),
               conflict_arguments.data(), production_services, output,
               error)
        != 0);
    assert(error.str().find("FSIM-TRACE-ARCHIVE-003") != std::string::npos);
    assert(!std::filesystem::exists(conflict_trace));
    output.str({ });
    error.str({ });
    assert(cli::run(static_cast<int>(simulate_arguments.size()),
               simulate_arguments.data(), production_services, output,
               error)
        == 0);
    assert(error.str().empty());
#if defined(FSIM_HAS_LLVM)
    assert(output.str().find("using forced-all AOT cache receipt")
        != std::string::npos);
    assert(std::filesystem::is_regular_file(*removed_native_object));
#endif
    assert(output.str().find("simulation stopped at tick 3") != std::string::npos);
    assert(std::filesystem::is_regular_file(trace));
    const auto compiled_fst_bytes = read_binary_file(trace);
    assert(compiled_fst_bytes.find("primary") != std::string::npos);
    const auto compiled_fst = runtime::read_fst(compiled_fst_bytes);
    assert(compiled_fst.ok());
    assert(compiled_fst.trace->final_time == 3U);
    assert(!compiled_fst.trace->declarations.empty());
    assert(!compiled_fst.trace->values.empty());
#if defined(FSIM_HAS_LLVM)
    assert(std::filesystem::is_directory(consumer_cache / "llvm-native"));
#else
    assert(!std::filesystem::exists(consumer_cache / "llvm-native"));
#endif
    assert(!std::filesystem::exists(design / "llvm-native"));
    assert(!std::filesystem::exists(design / "phase.fst"));
    std::cerr << "non-project cli: compiled CLI simulation validated\n";

    const std::vector<const char*> fst_simulate_arguments {
        "fsim", "simulate",
        "--design", design_text.c_str(),
        "--engine", "interpreter",
        "--duration", "10ns",
        "--max-deltas", "1000",
        "--trace", fst_trace_text.c_str(),
        "--trace-compression", "deterministic",
        "--trace-filter", "primary.*",
        "--cache", consumer_cache_text.c_str(),
        "--file-root", consumer_file_root_text.c_str()
    };
    output.str({ });
    error.str({ });
    assert(cli::run(static_cast<int>(fst_simulate_arguments.size()),
               fst_simulate_arguments.data(), production_services, output,
               error)
        == 0);
    assert(error.str().empty());
    const auto fst_bytes = read_binary_file(fst_trace);
    assert(fst_bytes == compiled_fst_bytes);
    const auto interpreter_fst = runtime::read_fst(fst_bytes);
    assert(interpreter_fst.ok());
    assert(interpreter_fst.trace->semantic_digest
        == compiled_fst.trace->semantic_digest);
    assert(fst_bytes.size() > 330U);
    assert(static_cast<unsigned char>(fst_bytes.front()) == 0U);
    assert(read_be64(fst_bytes, 1U) == 329U);
    assert(read_be64(fst_bytes, 17U) == 3U);
    const auto fst_frame = fst_initial_frame(fst_bytes);
    assert(fst_frame.handles > 0U && fst_frame.waves > 0U);
    assert(fst_frame.uncompressed > 0U && fst_frame.stored > 0U);
    assert(fst_frame.stored < fst_frame.uncompressed);
    assert(!std::filesystem::exists(fst_trace_text + ".fsim-lock"));
    assert(!std::filesystem::exists(design / "phase.fst"));

    runtime::FstLeafTypeMetadata coverage_metadata;
    coverage_metadata.kind = runtime::FstLeafKind::Coverage;
    coverage_metadata.owner_identity = "primary.covergroup.packet";
    coverage_metadata.leaf_path = "bins.accepted.hits";
    coverage_metadata.width = 64U;
    const auto coverage_value = runtime::encode_fst_leaf_value(
        runtime::PackedLogic4::from_aval_bval(64U, 17U, 0U), coverage_metadata);
    auto assertion_metadata = coverage_metadata;
    assertion_metadata.kind = runtime::FstLeafKind::Assertion;
    assertion_metadata.owner_identity = "primary.assertion.packet_valid";
    assertion_metadata.leaf_path = "attempts";
    const auto assertion_value = runtime::encode_fst_leaf_value(
        runtime::PackedLogic4::from_aval_bval(64U, 19U, 0U), assertion_metadata);
    assert(coverage_value.symbols().ends_with("10001"));
    assert(assertion_value.symbols().ends_with("10011"));
    assert(coverage_value.canonical_type() != assertion_value.canonical_type());

    runtime::FstLeafTypeMetadata container_metadata;
    container_metadata.kind = runtime::FstLeafKind::Container;
    container_metadata.owner_identity = "primary.queue@revision-4";
    container_metadata.leaf_path = "element[3].payload";
    container_metadata.width = 9U;
    container_metadata.four_state = true;
    container_metadata.dimensions = { { 0, 3, false } };
    const auto container_value = runtime::encode_fst_leaf_value(
        runtime::PackedLogic4::from_msb_string("10XZ10101"), container_metadata);
    auto class_metadata = container_metadata;
    class_metadata.kind = runtime::FstLeafKind::DynamicClass;
    class_metadata.owner_identity = "object-9@generation-2";
    class_metadata.leaf_path = "payload";
    class_metadata.dimensions.clear();
    const auto class_value = runtime::encode_fst_leaf_value(
        runtime::PackedLogic4::from_msb_string("01ZX01010"), class_metadata);
    assert(container_value.symbols() == "10xz10101");
    assert(class_value.symbols() == "01zx01010");
    assert(container_value.canonical_type() != class_value.canonical_type());
    runtime::FstChangeEncoder phase_changes;
    phase_changes.append(
        { { 2U }, 3U, 1U, runtime::TraceRegion::Observed, 2U }, assertion_value);
    phase_changes.append(
        { { 1U }, 3U, 0U, runtime::TraceRegion::Active, 1U }, coverage_value);
    const auto ordered_phase_changes = std::move(phase_changes).freeze();
    assert(ordered_phase_changes.values().size() == 2U);
    assert(ordered_phase_changes.values().front().event.signal.value == 1U);
    assert(ordered_phase_changes.values().back().event.signal.value == 2U);
    std::cerr << "non-project cli: inferred FST simulation validated\n";

    const std::vector<const char*> interpreter_simulate_arguments {
        "fsim", "simulate", "--design", design_text.c_str(),
        "--engine", "interpreter", "--duration", "2ns",
        "--seed", "11", "--delay-mode", "typ",
        "--trace-lifecycle", "disabled"
    };
    output.str({ });
    error.str({ });
    assert(cli::run(static_cast<int>(interpreter_simulate_arguments.size()),
               interpreter_simulate_arguments.data(), production_services,
               output, error)
        == 0);
    assert(error.str().empty());
    assert(output.str().find("simulation reached time limit at tick 2") != std::string::npos);

    const std::vector<const char*> debug_simulate_arguments {
        "fsim", "simulate", "--design", design_text.c_str(),
        "--engine", "debug", "--duration", "2ns",
        "--trace-lifecycle", "disabled"
    };
    output.str({ });
    error.str({ });
    assert(cli::run(static_cast<int>(debug_simulate_arguments.size()),
               debug_simulate_arguments.data(), production_services, output,
               error)
        == 0);
    assert(error.str().empty());
    assert(output.str().find("simulation reached time limit at tick 2") != std::string::npos);

    const std::vector<const char*> incompatible_delay_arguments {
        "fsim", "simulate", "--design", design_text.c_str(),
        "--delay-mode", "max"
    };
    output.str({ });
    error.str({ });
    assert(cli::run(static_cast<int>(incompatible_delay_arguments.size()),
               incompatible_delay_arguments.data(), production_services,
               output, error)
        != 0);
    assert(error.str().find("does not match the elaborated .fsimdesign") != std::string::npos);
    std::cerr << "non-project cli: interpreter and debug CLI validated\n";

    const auto provenance_cache = directory / "provenance-cache";
    std::filesystem::create_directories(provenance_cache);
    diagnostic::Engine cold_design_diagnostics;
    auto cold_design = app::load_design_artifact(design, cold_design_diagnostics);
    assert(cold_design && !cold_design_diagnostics.has_error());
    cold_design->cache_path = provenance_cache;
    app::Simulation cold_simulation { std::move(*cold_design), 1000,
        app::SimulationEngine::compiled };
    assert(provenance_signature(cold_simulation.verilog_scope_provenance()) == object_provenance_signature);
    const auto cold_cache = cold_simulation.native_cache_statistics();
#if defined(FSIM_HAS_LLVM)
    assert(cold_simulation.compiled_process_count() != 0);
    assert(cold_cache.misses != 0);
    assert(cold_cache.stores != 0);
#else
    assert(cold_simulation.compiled_process_count() == 0);
    assert(cold_cache.hits == 0 && cold_cache.misses == 0);
    assert(cold_cache.stores == 0);
#endif

    diagnostic::Engine warm_design_diagnostics;
    auto warm_design = app::load_design_artifact(design, warm_design_diagnostics);
    assert(warm_design && !warm_design_diagnostics.has_error());
    warm_design->cache_path = provenance_cache;
    app::Simulation warm_simulation { std::move(*warm_design), 1000,
        app::SimulationEngine::compiled };
    assert(provenance_signature(warm_simulation.verilog_scope_provenance()) == object_provenance_signature);
    const auto warm_cache = warm_simulation.native_cache_statistics();
#if defined(FSIM_HAS_LLVM)
    assert(warm_cache.hits != 0);
#else
    assert(warm_cache.hits == 0 && warm_cache.misses == 0);
    assert(warm_cache.stores == 0);
#endif

    diagnostic::Engine debug_design_diagnostics;
    auto debug_design = app::load_design_artifact(design, debug_design_diagnostics);
    assert(debug_design && !debug_design_diagnostics.has_error());
    debug_design->cache_path = provenance_cache;
    app::Simulation debug_simulation { std::move(*debug_design), 1000,
        app::SimulationEngine::debug };
    assert(provenance_signature(debug_simulation.verilog_scope_provenance()) == object_provenance_signature);
    const auto debug_cache = debug_simulation.native_cache_statistics();
#if defined(FSIM_HAS_LLVM)
    // Debug instrumentation changes both frame layout and restart-point code,
    // so it must not reuse the ordinary compiled cache entry.
    assert(debug_cache.hits == 0);
    assert(debug_cache.misses != 0);
    assert(debug_cache.stores != 0);
#else
    assert(debug_cache.hits == 0 && debug_cache.misses == 0);
    assert(debug_cache.stores == 0);
#endif

    const auto alternate_design = directory / "alternate.fsimdesign";
    copy_tree(design, alternate_design);
    make_tree_writable(alternate_design);
    auto alternate_metadata = *published_design_metadata;
    ++alternate_metadata.seed;
    alternate_metadata.design_digest = artifact::compute_design_digest(alternate_metadata);
    {
        std::ofstream alternate_output(
            alternate_design / artifact::kDesignMetadataFilename,
            std::ios::binary | std::ios::trunc);
        const auto alternate_bytes = artifact::serialize_design_metadata(alternate_metadata);
        alternate_output.write(alternate_bytes.data(), static_cast<std::streamsize>(alternate_bytes.size()));
        assert(alternate_output);
    }
    diagnostic::Engine alternate_design_diagnostics;
    auto alternate_loaded = app::load_design_artifact(alternate_design, alternate_design_diagnostics);
    assert(alternate_loaded && !alternate_design_diagnostics.has_error());
    assert(alternate_loaded->artifact_identity != published_design_metadata->design_digest);
    alternate_loaded->cache_path = provenance_cache;
    app::Simulation alternate_simulation { std::move(*alternate_loaded), 1000,
        app::SimulationEngine::compiled };
    const auto alternate_cache = alternate_simulation.native_cache_statistics();
#if defined(FSIM_HAS_LLVM)
    assert(alternate_cache.misses != 0);
    assert(alternate_cache.stores != 0);
#else
    assert(alternate_cache.hits == 0 && alternate_cache.misses == 0);
    assert(alternate_cache.stores == 0);
#endif

    const auto corrupt_trace_design = directory / "corrupt-trace.fsimdesign";
    copy_tree(design, corrupt_trace_design);
    make_tree_writable(corrupt_trace_design);
    auto corrupt_trace_metadata = *published_design_metadata;
    assert(!corrupt_trace_metadata.trace_archive.empty());
    corrupt_trace_metadata.trace_archive.back()
        = corrupt_trace_metadata.trace_archive.back() == '0' ? '1' : '0';
    corrupt_trace_metadata.design_digest = artifact::compute_design_digest(corrupt_trace_metadata);
    {
        std::ofstream corrupt_trace_output(
            corrupt_trace_design / artifact::kDesignMetadataFilename,
            std::ios::binary | std::ios::trunc);
        corrupt_trace_output
            << artifact::serialize_design_metadata(corrupt_trace_metadata);
        assert(corrupt_trace_output.good());
    }
    diagnostic::Engine corrupt_trace_diagnostics;
    assert(!app::load_design_artifact(
        corrupt_trace_design, corrupt_trace_diagnostics));
    assert(std::ranges::any_of(
        corrupt_trace_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-TRACE-ARCHIVE-002";
        }));

    const auto corrupt_design = directory / "corrupt.fsimdesign";
    copy_tree(design, corrupt_design);
    make_tree_writable(corrupt_design);
    {
        std::ofstream corrupt_runtime(corrupt_design / "state/runtime.bin",
            std::ios::binary | std::ios::app);
        corrupt_runtime.put('x');
    }
    diagnostic::Engine corrupt_design_diagnostics;
    assert(
        !app::load_design_artifact(corrupt_design, corrupt_design_diagnostics));
    assert(corrupt_design_diagnostics.has_error());

    const auto partial_design = directory / "partial.fsimdesign";
    copy_tree(design, partial_design);
    make_tree_writable(partial_design);
    assert(std::filesystem::remove(partial_design / "state/semantics.bin"));
    diagnostic::Engine partial_design_diagnostics;
    assert(
        !app::load_design_artifact(partial_design, partial_design_diagnostics));
    assert(partial_design_diagnostics.has_error());

    const auto incompatible_design = directory / "incompatible.fsimdesign";
    copy_tree(design, incompatible_design);
    make_tree_writable(incompatible_design);
    auto incompatible_metadata = *published_design_metadata;
    ++incompatible_metadata.runtime_abi;
    incompatible_metadata.design_digest = artifact::compute_design_digest(incompatible_metadata);
    {
        std::ofstream incompatible_output(
            incompatible_design / artifact::kDesignMetadataFilename,
            std::ios::binary | std::ios::trunc);
        const auto incompatible_bytes = artifact::serialize_design_metadata(incompatible_metadata);
        incompatible_output.write(
            incompatible_bytes.data(),
            static_cast<std::streamsize>(incompatible_bytes.size()));
        assert(incompatible_output);
    }
    diagnostic::Engine incompatible_design_diagnostics;
    assert(!app::load_design_artifact(incompatible_design,
        incompatible_design_diagnostics));
    assert(incompatible_design_diagnostics.has_error());

    const auto relocated_read_only_root = directory
        / support::path_from_utf8("relocated artifacts \xc2\xb5");
    const auto relocated_read_only_design = relocated_read_only_root
        / "read only design.fsimdesign";
    const auto relocated_trace_directory = directory
        / support::path_from_utf8("relocated outputs \xc2\xb5");
    const auto relocated_trace = relocated_trace_directory / "phase.fst";
    const auto relocated_cache = directory / "relocated-consumer-cache";
    const auto relocated_file_root = directory / "relocated-consumer-files";
    std::filesystem::create_directories(relocated_read_only_root);
    std::filesystem::create_directories(relocated_trace_directory);
    std::filesystem::create_directories(relocated_cache);
    std::filesystem::create_directories(relocated_file_root);
    copy_tree(design, relocated_read_only_design);
    const auto producer_prefix = support::path_to_utf8(directory);
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             relocated_read_only_design)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto bytes = read_binary_file(entry.path());
        assert(bytes.find(producer_prefix) == std::string::npos);
    }
    make_tree_read_only(relocated_read_only_root);
    const auto relocated_read_only_design_text
        = support::path_to_utf8(relocated_read_only_design);
    const auto relocated_trace_text = support::path_to_utf8(relocated_trace);
    const auto relocated_cache_text = support::path_to_utf8(relocated_cache);
    const auto relocated_file_root_text
        = support::path_to_utf8(relocated_file_root);
    const std::vector<const char*> relocated_arguments {
        "fsim", "simulate", "--design", relocated_read_only_design_text.c_str(),
        "--engine", "interpreter", "--duration", "10ns", "--max-deltas",
        "1000", "--trace", relocated_trace_text.c_str(), "--cache",
        relocated_cache_text.c_str(), "--file-root",
        relocated_file_root_text.c_str(), "--trace-compression",
        "deterministic", "--trace-filter", "primary.*"
    };
    output.str({ });
    error.str({ });
    const auto relocated_result = cli::run(
        static_cast<int>(relocated_arguments.size()),
        relocated_arguments.data(), production_services, output, error);
    if (relocated_result != 0) {
        std::cerr << error.str();
    }
    assert(relocated_result == 0);
    assert(error.str().empty());
    assert(output.str().find("simulation stopped at tick 3")
        != std::string::npos);
    assert(std::filesystem::is_regular_file(relocated_trace));
    assert(runtime::read_fst(read_binary_file(relocated_trace)).ok());
    assert(!std::filesystem::exists(
        relocated_read_only_design / "llvm-native"));
    assert(!std::filesystem::exists(
        relocated_read_only_design / "phase.fst"));
    make_tree_writable(relocated_read_only_root);

    std::filesystem::rename(hidden_object, object);
    std::filesystem::rename(hidden_extra_object, extra_object);
    std::filesystem::rename(hidden_source, source);
    std::filesystem::rename(hidden_extra_source, extra_source);

    const auto corrupt_object = directory / "corrupt.fsimobj";
    std::filesystem::create_directory(corrupt_object);
    for (const auto& entry :
        std::filesystem::recursive_directory_iterator(object)) {
        const auto relative = std::filesystem::relative(entry.path(), object);
        if (entry.is_directory()) {
            std::filesystem::create_directories(corrupt_object / relative);
        } else {
            std::filesystem::copy_file(entry.path(), corrupt_object / relative);
        }
    }
    const auto corrupt_unit = corrupt_object / indexed_unit.artifact;
    std::filesystem::permissions(corrupt_unit,
        std::filesystem::perms::owner_write,
        std::filesystem::perm_options::add);
    {
        std::ofstream corrupt_output(corrupt_unit,
            std::ios::binary | std::ios::app);
        corrupt_output.put('x');
    }
    diagnostic::Engine corrupt_diagnostics;
    const std::vector corrupt_inputs { corrupt_object };
    assert(!app::load_objects(corrupt_inputs, corrupt_diagnostics));
    assert(corrupt_diagnostics.has_error());

    diagnostic::Engine duplicate_diagnostics;
    const std::vector duplicate_inputs { object, object };
    assert(!app::load_objects(duplicate_inputs, duplicate_diagnostics));
    assert(duplicate_diagnostics.has_error());

    const auto vhdl_source = directory / "ordered.vhd";
    const auto vhdl_object = directory / "ordered.fsimobj";
    const auto vhdl_source_text = support::path_to_utf8(vhdl_source);
    const auto vhdl_object_text = support::path_to_utf8(vhdl_object);
    {
        std::ofstream vhdl_output(vhdl_source);
        vhdl_output << R"(
package SharedPkg is
  constant FLAG : boolean := true;
end package;
use work.SharedPkg.all;
entity MixedCase is end entity;
architecture rtl of MixedCase is
begin
  process begin
    assert FLAG;
    wait;
  end process;
end architecture;
)";
    }
    const std::vector<const char*> vhdl_compile_arguments {
        "fsim",
        "compile",
        "--lang",
        "vhdl",
        "--standard",
        "2008",
        "--library",
        "work",
        "--compilation-unit",
        "file",
        "--output",
        vhdl_object_text.c_str(),
        vhdl_source_text.c_str()
    };
    output.str({ });
    error.str({ });
    const auto vhdl_compile_result = cli::run(
        static_cast<int>(vhdl_compile_arguments.size()),
        vhdl_compile_arguments.data(), production_services, output, error);
    if (vhdl_compile_result != 0) {
        std::cerr << error.str();
    }
    assert(vhdl_compile_result == 0);
    assert(error.str().empty());
    diagnostic::Engine vhdl_metadata_diagnostics;
    const auto vhdl_metadata = artifact::load_object_metadata(vhdl_object, vhdl_metadata_diagnostics);
    assert(vhdl_metadata && !vhdl_metadata_diagnostics.has_error());
    assert(vhdl_metadata->units.size() == 3);
    assert(vhdl_metadata->units[0].name == "sharedpkg");
    assert(vhdl_metadata->units[1].name == "mixedcase");
    assert(vhdl_metadata->units[2].primary_name == "mixedcase");

    auto package_metadata = *vhdl_metadata;
    package_metadata.units = { vhdl_metadata->units[0] };
    package_metadata.compilation_digest = artifact::compute_object_compilation_digest(package_metadata);
    auto design_metadata = *vhdl_metadata;
    design_metadata.units = { vhdl_metadata->units[1], vhdl_metadata->units[2] };
    design_metadata.compilation_digest = artifact::compute_object_compilation_digest(design_metadata);
    const auto package_object = directory / "package.fsimobj";
    const auto design_object = directory / "design-units.fsimobj";
    diagnostic::Engine package_publish_diagnostics;
    assert(
        artifact::publish_object(package_object, package_metadata,
            object_payloads(vhdl_object, package_metadata),
            package_publish_diagnostics));
    assert(!package_publish_diagnostics.has_error());
    diagnostic::Engine design_publish_diagnostics;
    assert(artifact::publish_object(design_object, design_metadata,
        object_payloads(vhdl_object, design_metadata),
        design_publish_diagnostics));
    assert(!design_publish_diagnostics.has_error());

    diagnostic::Engine ordered_vhdl_diagnostics;
    const std::vector ordered_vhdl_inputs { package_object, design_object };
    const auto ordered_vhdl = app::load_objects(ordered_vhdl_inputs, ordered_vhdl_diagnostics);
    assert(ordered_vhdl && !ordered_vhdl_diagnostics.has_error());
    assert(ordered_vhdl->objects.size() == 2U);
    assert(std::ranges::all_of(ordered_vhdl->objects, [](const auto& provenance) {
        return provenance.language == "vhdl" && provenance.standard == "2008" && provenance.compatibility_profile == "fsim-synopsys-ieee-compat-v2";
    }));
    diagnostic::Engine reversed_vhdl_diagnostics;
    const std::vector reversed_vhdl_inputs { design_object, package_object };
    assert(!app::load_objects(reversed_vhdl_inputs, reversed_vhdl_diagnostics));
    assert(reversed_vhdl_diagnostics.has_error());

    auto wrong_library_metadata = package_metadata;
    wrong_library_metadata.library = "other";
    wrong_library_metadata.compilation_digest = artifact::compute_object_compilation_digest(wrong_library_metadata);
    const auto wrong_library_object = directory / "wrong-library.fsimobj";
    diagnostic::Engine wrong_publish_diagnostics;
    assert(
        artifact::publish_object(wrong_library_object, wrong_library_metadata,
            object_payloads(vhdl_object, package_metadata),
            wrong_publish_diagnostics));
    diagnostic::Engine wrong_load_diagnostics;
    const std::vector wrong_library_inputs { wrong_library_object };
    assert(!app::load_objects(wrong_library_inputs, wrong_load_diagnostics));
    assert(wrong_load_diagnostics.has_error());

    assert(std::filesystem::remove(vhdl_source));
    std::filesystem::rename(vhdl_object,
        directory / "ordered.fsimobj.producer-hidden");
    assert(!std::filesystem::exists(vhdl_object));
    diagnostic::Engine standalone_vhdl_diagnostics;
    const auto standalone_vhdl = app::load_objects(ordered_vhdl_inputs, standalone_vhdl_diagnostics);
    assert(standalone_vhdl && !standalone_vhdl_diagnostics.has_error());
    assert(standalone_vhdl->objects.size() == ordered_vhdl->objects.size());
    for (std::size_t index = 0; index < ordered_vhdl->objects.size(); ++index) {
        const auto& expected = ordered_vhdl->objects[index];
        const auto& actual = standalone_vhdl->objects[index];
        assert(actual.metadata_digest == expected.metadata_digest);
        assert(actual.compilation_digest == expected.compilation_digest);
        assert(actual.standard == expected.standard);
        assert(actual.compatibility_profile == expected.compatibility_profile);
        assert(actual.vhdl_package_dependencies == expected.vhdl_package_dependencies);
    }

    const auto isolated_source = directory / "isolated.sv";
    const auto isolated_object = directory / "isolated.fsimobj";
    const auto isolated_source_text = support::path_to_utf8(isolated_source);
    const auto isolated_object_text = support::path_to_utf8(isolated_object);
    {
        std::ofstream isolated_output(isolated_source);
        isolated_output << R"(`ifdef LEAK
module leaked; endmodule
`else
module isolated; endmodule
`endif
)";
    }
    const std::vector<const char*> isolated_arguments {
        "fsim",
        "compile",
        "--lang",
        "systemverilog",
        "--standard",
        "2017",
        "--library",
        "work",
        "--output",
        isolated_object_text.c_str(),
        isolated_source_text.c_str()
    };
    output.str({ });
    error.str({ });
    assert(cli::run(static_cast<int>(isolated_arguments.size()),
               isolated_arguments.data(), production_services, output,
               error)
        == 0);
    diagnostic::Engine isolated_diagnostics;
    const auto isolated_metadata = artifact::load_object_metadata(isolated_object, isolated_diagnostics);
    assert(isolated_metadata && isolated_metadata->units.size() == 1);
    assert(isolated_metadata->units.front().name == "isolated");

    output.str({ });
    error.str({ });
    assert(cli::run(static_cast<int>(compile_arguments.size()),
               compile_arguments.data(), production_services, output,
               error)
        == 1);
    assert(error.str().find("already exists") != std::string::npos);
}

} // namespace fsim::test
