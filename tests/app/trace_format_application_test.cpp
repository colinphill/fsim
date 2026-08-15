// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/cli/driver.hpp"
#include "fsim/runtime/fst_compression.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
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

struct RunResult {
    int status { };
    std::string output;
    std::string error;
};

void write_text(
    const std::filesystem::path& path,
    const std::string_view contents)
{
    std::ofstream output { path, std::ios::binary | std::ios::trunc };
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    assert(output);
}

[[nodiscard]] std::string read_bytes(const std::filesystem::path& path)
{
    std::ifstream input { path, std::ios::binary };
    assert(input);
    return {
        std::istreambuf_iterator<char> { input },
        std::istreambuf_iterator<char> { }
    };
}

[[nodiscard]] std::uint64_t read_be64(
    const std::string& bytes,
    const std::size_t offset)
{
    assert(offset + 8U <= bytes.size());
    std::uint64_t result = 0;
    for (std::size_t index = 0; index < 8U; ++index) {
        result = (result << 8U)
            | static_cast<unsigned char>(bytes[offset + index]);
    }
    return result;
}

[[nodiscard]] std::uint64_t read_varint(
    const std::string& bytes,
    std::size_t& offset)
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

[[nodiscard]] std::filesystem::path write_manifest(
    const std::filesystem::path& directory,
    const std::string_view identity,
    const std::string_view trace_file,
    const std::string_view trace_format = { },
    const std::string_view source_file = "trace_format.sv")
{
    const auto manifest = directory / (std::string { identity } + ".toml");
    std::ostringstream text;
    text << "schema = 2\n"
         << "[project]\n"
         << "name = \"trace-format\"\n"
         << "top = \"sv:work.trace_format\"\n"
         << "time_resolution = \"1ns\"\n\n"
         << "[[source_set]]\n"
         << "language = \"systemverilog\"\n"
         << "standard = \"2017\"\n"
         << "library = \"work\"\n"
         << "files = [\"" << source_file << "\"]\n\n"
         << "[build]\n"
         << "optimization = \"O0\"\n"
         << "cache_path = \"cache\"\n\n"
         << "[run]\n"
         << "max_deltas = 1000\n"
         << "trace_file = \"" << trace_file << "\"\n";
    if (!trace_format.empty()) {
        text << "trace_format = \"" << trace_format << "\"\n";
    }
    write_text(manifest, text.str());
    return manifest;
}

[[nodiscard]] std::filesystem::path write_vhdl_manifest(
    const std::filesystem::path& directory)
{
    const auto manifest = directory / "logic9.toml";
    write_text(manifest, R"toml(schema = 2
[project]
name = "logic9-trace"
top = "vhdl:work.trace_format(rtl)"
time_resolution = "1ns"

[[source_set]]
language = "vhdl"
standard = "2008"
library = "work"
files = ["logic9_trace.vhd"]

[build]
optimization = "O0"
cache_path = "logic9-cache"

[run]
duration = "1ns"
max_deltas = 1000
trace_file = "logic9.fst"
trace_format = "fst"
)toml");
    return manifest;
}

[[nodiscard]] RunResult run_manifest(
    const std::filesystem::path& manifest)
{
    const auto manifest_text = manifest.string();
    const std::vector<const char*> arguments {
        "fsim", "run", "--project", manifest_text.c_str()
    };
    std::ostringstream output;
    std::ostringstream error;
    const auto status = fsim::cli::run(
        static_cast<int>(arguments.size()),
        arguments.data(),
        fsim::app::make_cli_services(),
        output,
        error);
    return { status, output.str(), error.str() };
}

void test_public_format_model()
{
    using Format = fsim::project::TraceFormat;
    assert(fsim::project::RunSection { }.trace_format == Format::automatic);
    assert(fsim::project::parse_trace_format("auto") == Format::automatic);
    assert(fsim::project::parse_trace_format("AUTOMATIC") == Format::automatic);
    assert(fsim::project::parse_trace_format("VCD") == Format::vcd);
    assert(fsim::project::parse_trace_format("fst") == Format::fst);
    assert(!fsim::project::parse_trace_format("wave"));
    assert(fsim::project::to_string(Format::automatic) == "auto");
    assert(fsim::project::to_string(Format::vcd) == "vcd");
    assert(fsim::project::to_string(Format::fst) == "fst");
}

void test_vcd_defaults_and_replacement(
    const std::filesystem::path& directory)
{
    const auto default_path = directory / "default.vcd";
    write_text(default_path, "previous-default");
    const auto default_result = run_manifest(write_manifest(
        directory, "default", default_path.filename().string()));
    assert(default_result.status == 0 && default_result.error.empty());
    const auto default_vcd = read_bytes(default_path);
    assert(default_vcd.starts_with("$version fsim $end\n"));
    assert(default_vcd.find("$timescale 1ns $end") != std::string::npos);
    assert(!std::filesystem::exists(default_path.string() + ".fsim-lock"));

    const auto automatic_path = directory / "automatic.vcd";
    const auto automatic_result = run_manifest(write_manifest(
        directory, "automatic", automatic_path.filename().string(), "auto"));
    assert(automatic_result.status == 0 && automatic_result.error.empty());
    assert(read_bytes(automatic_path) == default_vcd);

    const auto explicit_path = directory / "explicit.wave";
    const auto explicit_result = run_manifest(write_manifest(
        directory, "explicit", explicit_path.filename().string(), "vcd"));
    assert(explicit_result.status == 0 && explicit_result.error.empty());
    assert(read_bytes(explicit_path) == default_vcd);

    const auto inferred_path = directory / "inferred.wave";
    const auto inferred_result = run_manifest(write_manifest(
        directory, "inferred", inferred_path.filename().string()));
    assert(inferred_result.status == 0 && inferred_result.error.empty());
    assert(read_bytes(inferred_path) == default_vcd);
}

void check_fst(
    const std::filesystem::path& path,
    const std::uint64_t expected_final_time = 2U)
{
    const auto bytes = read_bytes(path);
    assert(bytes.size() > 330U);
    assert(static_cast<unsigned char>(bytes.front()) == 0U);
    assert(read_be64(bytes, 1U) == 329U);
    assert(read_be64(bytes, 9U) == 0U);
    assert(read_be64(bytes, 17U) == expected_final_time);
    assert(static_cast<unsigned char>(bytes[73U]) == 0xf7U);
    assert(bytes.substr(74U,
               fsim::runtime::kFstDeterministicContainerProfile.size())
        == fsim::runtime::kFstDeterministicContainerProfile);
    std::size_t frame = 363U;
    const auto bits = read_varint(bytes, frame);
    const auto stored_bits = read_varint(bytes, frame);
    assert(stored_bits != 0U && (stored_bits == bits || bits >= 128U));
    const auto handles = read_varint(bytes, frame);
    assert(handles >= 4U && frame + stored_bits < bytes.size());
    frame += static_cast<std::size_t>(stored_bits);
    assert(read_varint(bytes, frame) >= 4U);
}

void test_fst_inference_and_selection(
    const std::filesystem::path& directory)
{
    const auto inferred_path = directory / "inferred.fst";
    const auto inferred_result = run_manifest(write_manifest(
        directory, "fst-inferred", inferred_path.filename().string()));
    if (inferred_result.status != 0) {
        std::cerr << inferred_result.error;
    }
    assert(inferred_result.status == 0 && inferred_result.error.empty());
    check_fst(inferred_path);

    const auto case_path = directory / "case.FST";
    const auto case_result = run_manifest(write_manifest(
        directory, "fst-case", case_path.filename().string(), "auto"));
    assert(case_result.status == 0 && case_result.error.empty());
    check_fst(case_path);

    const auto explicit_path = directory / "explicit.bin";
    const auto explicit_result = run_manifest(write_manifest(
        directory, "fst-explicit", explicit_path.filename().string(), "fst"));
    assert(explicit_result.status == 0 && explicit_result.error.empty());
    check_fst(explicit_path);
}

void test_configuration_failures_preserve_destinations(
    const std::filesystem::path& directory)
{
    const auto fst_conflict = directory / "fst-conflict.vcd";
    write_text(fst_conflict, "keep-fst-conflict");
    const auto fst_result = run_manifest(write_manifest(
        directory, "fst-conflict", fst_conflict.filename().string(), "fst"));
    assert(fst_result.status == 1);
    assert(fst_result.error.find("FSIM-TRACE-CONTROL-002")
        != std::string::npos);
    assert(read_bytes(fst_conflict) == "keep-fst-conflict");

    const auto vcd_conflict = directory / "vcd-conflict.fst";
    write_text(vcd_conflict, "keep-vcd-conflict");
    const auto vcd_result = run_manifest(write_manifest(
        directory, "vcd-conflict", vcd_conflict.filename().string(), "vcd"));
    assert(vcd_result.status == 1);
    assert(vcd_result.error.find("FSIM-TRACE-CONTROL-002")
        != std::string::npos);
    assert(read_bytes(vcd_conflict) == "keep-vcd-conflict");

    const auto unknown_path = directory / "unknown.fst";
    const auto unknown_result = run_manifest(write_manifest(
        directory, "unknown", unknown_path.filename().string(), "wave"));
    assert(unknown_result.status == 1);
    assert(unknown_result.error.find("trace_format") != std::string::npos);
    assert(!std::filesystem::exists(unknown_path));
}

void test_extended_typed_values(const std::filesystem::path& directory)
{
    const auto typed_target = directory / "typed.fst";
    const auto typed_result = run_manifest(write_manifest(
        directory,
        "typed-target",
        typed_target.filename().string(),
        "fst",
        "typed_trace.sv"));
    assert(typed_result.status == 0 && typed_result.error.empty());
    check_fst(typed_target, 1U);

    const auto bytes = read_bytes(typed_target);
    assert(read_be64(bytes, 57U) >= 12U);
    assert(bytes.find("real_value") != std::string::npos);
    assert(bytes.find("short_value") != std::string::npos);
    assert(bytes.find("realtime_value") != std::string::npos);
    assert(bytes.find("state_value") != std::string::npos);
    assert(bytes.find("string_value") != std::string::npos);
    assert(bytes.find("typed-string") != std::string::npos);
    assert(bytes.find("xxx") != std::string::npos);
}

void test_logic9_value(const std::filesystem::path& directory)
{
    const auto result = run_manifest(write_vhdl_manifest(directory));
    assert(result.status == 0 && result.error.empty());
    const auto path = directory / "logic9.fst";
    check_fst(path, 1U);
    const auto bytes = read_bytes(path);
    assert(bytes.find(std::string_view { "logic9_value\0\x24", 14U })
        != std::string::npos);
}

void test_unsafe_outputs_are_transactional(
    const std::filesystem::path& directory)
{
    const auto directory_target = directory / "directory.vcd";
    std::filesystem::create_directory(directory_target);
    const auto directory_result = run_manifest(write_manifest(
        directory, "directory-target", directory_target.filename().string()));
    assert(directory_result.status == 1);
    assert(directory_result.error.find("FSIM-VCD-0002") != std::string::npos);
    assert(std::filesystem::is_directory(directory_target));

    const auto locked_target = directory / "locked.vcd";
    write_text(locked_target, "keep-locked");
    const auto lock = std::filesystem::path {
        locked_target.string() + ".fsim-lock"
    };
    std::filesystem::create_directory(lock);
    const auto locked_result = run_manifest(write_manifest(
        directory, "locked-target", locked_target.filename().string()));
    assert(locked_result.status == 1);
    assert(locked_result.error.find("FSIM-VCD-0002") != std::string::npos);
    assert(read_bytes(locked_target) == "keep-locked");
    assert(std::filesystem::is_directory(lock));
}

void test_terminal_publish_failure(
    const std::filesystem::path& directory)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "terminal-trace";
    config.project.top = "sv:work.trace_format";
    config.project.time_resolution = "1ns";
    config.build.cache_path = directory / "terminal-cache";
    config.run.max_deltas = 1'000U;
    config.run.trace_file = directory / "terminal.fst";
    config.run.trace_format = fsim::project::TraceFormat::fst;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files = { directory / "trace_format.sv" };
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    assert(project && !diagnostics.has_error());
    fsim::app::Simulation simulation(
        std::move(*project), config.run.max_deltas,
        fsim::app::SimulationEngine::debug);
    std::ostringstream output;
    std::ostringstream error;
    {
        fsim::app::DebuggerControl debugger(
            simulation, output, error, config, diagnostics);
        std::filesystem::create_directory(*config.run.trace_file);
    }
    const auto terminal_count = std::ranges::count_if(
        diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-TRACE-0003";
        });
    assert(terminal_count == 1);
    assert(std::filesystem::is_directory(*config.run.trace_file));
    assert(!std::filesystem::exists(std::filesystem::path {
        config.run.trace_file->string() + ".fsim-lock" }));
    assert(error.str().empty());
}

} // namespace

int main()
{
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("ff-" + std::to_string(nonce))
    };
    std::filesystem::create_directories(directory.path);
    write_text(directory.path / "trace_format.sv", R"(
module trace_format;
  logic value;
  bit two_state;
  time time_value;
  chandle handle_value;
  initial begin
    value = 1'b0;
    #1 value = 1'b1;
    #1 $finish;
  end
endmodule
)");
    write_text(directory.path / "typed_trace.sv", R"(
module trace_format;
  typedef enum logic [2:0] { IDLE = 3'b001, BUSY = 3'b010 } state_t;
  real real_value = -0.0;
  shortreal short_value = 1.5;
  realtime realtime_value = 2.25;
  string string_value = "typed-string";
  state_t state_value = BUSY;
  initial begin
    #1 $finish;
  end
endmodule
)");
    write_text(directory.path / "logic9_trace.vhd", R"(
library ieee;
use ieee.std_logic_1164.all;

entity trace_format is
end entity;

architecture rtl of trace_format is
  signal logic9_value : std_logic_vector(8 downto 0);
begin
end architecture;
)");

    test_public_format_model();
    test_vcd_defaults_and_replacement(directory.path);
    test_fst_inference_and_selection(directory.path);
    test_configuration_failures_preserve_destinations(directory.path);
    test_extended_typed_values(directory.path);
    test_logic9_value(directory.path);
    test_unsafe_outputs_are_transactional(directory.path);
    test_terminal_publish_failure(directory.path);
    return 0;
}
