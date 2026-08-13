// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/runtime/file_scanning.hpp"
#include "fsim/runtime/vcd_writer.hpp"
#include "path_test_support.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
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
    std::string output_file;
    std::string monitor_file;
    std::string multichannel_first;
    std::string multichannel_second;
    std::string saved_line;
    std::string binary_word;
    std::string positioned_word;
    std::string memory_hex_dump;
    std::string memory_binary_dump;
    std::string packet_fread_dump;
    std::string packet_memory_dump;
    std::string multidimensional_memory_dump;
    std::string string_memory_dump;
    std::string aggregate_memory_dump;
    std::string multidimensional_fread_dump;
    std::string aggregate_fread_dump;
    std::vector<std::string> binary_memory;
    std::vector<std::string> packet_memory;
    std::vector<std::string> keys;
    std::vector<fsim::runtime::simir::ExecutionPoint> points;
    fsim::app::NativeCacheStatistics cache;
    std::size_t compiled_processes { };
};

void write_text(
    const std::filesystem::path& path,
    const std::string_view text)
{
    std::ofstream output(
        path, std::ios::binary | std::ios::trunc);
    output << text;
    assert(output.good());
}

[[nodiscard]] std::string read_text(
    const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char> { input },
        std::istreambuf_iterator<char> { }
    };
}

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& stable,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "sv-files";
    config.project.top = "sv:work.file_top";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "cache-o0"
                : "cache-o2");
    config.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.compilation_unit = "file";
    sources.files = { stable, source };
    config.source_sets.push_back(std::move(sources));
    return config;
}

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(project);
    fsim::diagnostic::Engine artifact_diagnostics;
    const auto encoded = fsim::app::serialize_runtime_state(
        project->design, artifact_diagnostics);
    assert(encoded && !artifact_diagnostics.has_error());
    auto restored = fsim::app::deserialize_runtime_state(
        *encoded, "sv-file-runtime", artifact_diagnostics);
    assert(restored && !artifact_diagnostics.has_error());
    assert(fsim::app::serialize_runtime_state(
               *restored, artifact_diagnostics)
        == encoded);
    project->design = std::move(*restored);
    Capture capture;
    capture.keys = project->specialization_cache_keys;
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    capture.compiled_processes = simulation.compiled_process_count();
    capture.cache = simulation.native_cache_statistics();
    simulation.set_execution_point_hook(
        [&capture](
            fsim::runtime::Scheduler&,
            const fsim::runtime::simir::ExecutionPoint& point) {
            capture.points.push_back(point);
        });
    capture.result = simulation.run();
    const auto saved = std::ranges::find_if(
        simulation.design().string_objects(),
        [](const auto& object) {
            return object.name == "file_top.saved";
        });
    assert(saved != simulation.design().string_objects().end());
    capture.saved_line = simulation.read_string_object(saved->id);
    const auto binary_word = simulation.find_signal("file_top.binary_word");
    const auto positioned_word = simulation.find_signal("file_top.positioned_word");
    const auto binary_memory = simulation.design().find_container("file_top.binary_memory");
    const auto packet_memory = simulation.design().find_container("file_top.packet_memory");
    assert(binary_word && positioned_word && binary_memory && packet_memory);
    capture.binary_word = simulation.read_signal(*binary_word).to_msb_string();
    capture.positioned_word = simulation.read_signal(*positioned_word).to_msb_string();
    for (const auto& element :
        simulation.read_container_object(*binary_memory).elements) {
        capture.binary_memory.push_back(element.to_msb_string());
    }
    for (const auto& element :
        simulation.read_container_object(*packet_memory).elements) {
        capture.packet_memory.push_back(element.to_msb_string());
    }
    capture.output_file = read_text(config.base_directory / "output.txt");
    capture.monitor_file = read_text(config.base_directory / "monitor.txt");
    capture.multichannel_first = read_text(config.base_directory / "multichannel-first.txt");
    capture.multichannel_second = read_text(config.base_directory / "multichannel-second.txt");
    capture.memory_hex_dump = read_text(config.base_directory / "dump.hex");
    capture.memory_binary_dump = read_text(config.base_directory / "dump.bin");
    capture.packet_fread_dump = read_text(config.base_directory / "packets-fread.hex");
    capture.packet_memory_dump = read_text(config.base_directory / "packets-dump.hex");
    capture.multidimensional_memory_dump = read_text(config.base_directory / "multidimensional-dump.hex");
    capture.string_memory_dump = read_text(config.base_directory / "strings-dump.mem");
    capture.aggregate_memory_dump = read_text(config.base_directory / "aggregates-dump.hex");
    capture.multidimensional_fread_dump = read_text(config.base_directory / "multidimensional-fread.hex");
    capture.aggregate_fread_dump = read_text(config.base_directory / "aggregates-fread.hex");
    return capture;
}

void verify_suspension(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    assert(project);
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    std::optional<fsim::runtime::simir::ProcessId> process_id;
    std::optional<std::size_t> handle_local;
    for (const auto& process : simulation.design().processes()) {
        for (std::size_t index = 0;
            index < process.debug_locals.size(); ++index) {
            if (process.debug_locals[index].name
                == "read_one.file_handle") {
                process_id = process.id;
                handle_local = index;
            }
        }
    }
    assert(process_id && handle_local);
    bool armed = false;
    bool requested = false;
    simulation.set_execution_point_hook(
        [&](fsim::runtime::Scheduler& scheduler,
            const fsim::runtime::simir::ExecutionPoint& point) {
            if (point.process != *process_id) {
                return;
            }
            if (point.kind
                == fsim::runtime::simir::ExecutionPointKind::wait) {
                armed = true;
            } else if (
                armed && !requested
                && point.kind
                    == fsim::runtime::simir::
                        ExecutionPointKind::process_suspend) {
                requested = true;
                scheduler.request_stop();
            }
        });
    const auto stopped = simulation.run();
    assert(requested);
    assert(stopped.status == fsim::runtime::RunStatus::stopped);
    assert(stopped.time == 0);
    const auto handle = simulation.read_process_local(
                                      *process_id, *handle_local)
                            .low_word();
    assert(handle.bval == 0 && handle.aval != 0);
    simulation.clear_stop();
    const auto resumed = simulation.run();
    assert(resumed.status == fsim::runtime::RunStatus::completed);
    assert(
        read_text(config.base_directory / "output.txt")
        == "value=7\ntail0111\na5\n245\n0111a5245\n"
           "strobe=file_top/0/a5\n1010\na5\n245\n"
           "|chars=97/81/81/90/0/-1/1\n"
           "scan=2/13/alpha/2/42/done\n"
           "scalar=1.25/17/0/3\n"
           "formatted=1.25/17/0\n"
           "wide=10x23456789abcdefz123456789abcdef/1\n"
           "wide-direct=10x23456789abcdefz123456789abcdef\n"
           "binary=4/305419896/4/0/2/22136/0/2/18/52\n"
           "binary_scalar=1.25/23/0/8/8/8\n");
}

void verify_bad_path(
    const fsim::project::Config& config,
    const std::filesystem::path& source,
    const fsim::app::SimulationEngine engine)
{
    write_text(
        source,
        R"(
module file_top;
  integer handle;
  initial handle = $fopen("../escape.txt", "w");
endmodule
)");
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    assert(project);
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    try {
        static_cast<void>(simulation.run());
        assert(false);
    } catch (const std::exception& error) {
        assert(
            std::string { error.what() }.find(
                "manifest root")
            != std::string::npos);
    }
}

void verify_fstrobe_expression_diagnostic(
    const fsim::project::Config& config,
    const std::filesystem::path& source)
{
    write_text(
        source,
        R"(
module file_top;
  integer handle;
  logic [7:0] value;
  initial begin
    handle = $fopen("invalid-fstrobe.txt", "w");
    $fstrobe(handle, "%0h", value + 1'b1);
  end
endmodule
)");
    fsim::diagnostic::Engine diagnostics;
    const auto project = fsim::app::build_project(config, diagnostics);
    assert(!project);
    assert(std::ranges::any_of(
        diagnostics.diagnostics(),
        [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-SVFILE-007";
        }));
}

fsim::project::Config make_vhdl_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization,
    const std::string_view top = "vhdl_file_top")
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = std::string { top };
    config.project.top = "vhdl:work." + std::string { top } + "(rtl)";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "vhdl-cache-o0"
                : "vhdl-cache-o2");
    config.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.files = { source };
    config.source_sets.push_back(std::move(sources));
    return config;
}

void verify_vhdl_textio_failure(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization,
    const fsim::app::SimulationEngine engine)
{
    const auto config = make_vhdl_config(
        directory, source, optimization, "vhdl_textio_failure");
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    assert(project);
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    bool failed { };
    try {
        static_cast<void>(simulation.run());
    } catch (const std::exception& error) {
        failed = true;
        assert(
            std::string_view { error.what() }.find(
                "VHDL file read did not convert the requested element")
            != std::string_view::npos);
    }
    assert(failed);
}

fsim::app::NativeCacheStatistics verify_vhdl_file_objects(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization,
    const fsim::app::SimulationEngine engine)
{
    const auto config = make_vhdl_config(
        directory, source, optimization);
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(project);
    assert(project->specialization_cache_keys.size() == 1);
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    const auto cache = simulation.native_cache_statistics();
    std::vector<fsim::runtime::simir::ExecutionPoint> points;
    simulation.set_execution_point_hook(
        [&](fsim::runtime::Scheduler&,
            const fsim::runtime::simir::ExecutionPoint& point) {
            points.push_back(point);
        });
    const auto result = simulation.run();
    assert(result.status == fsim::runtime::RunStatus::completed);
    const auto done = simulation.find_signal("vhdl_file_top.done");
    assert(done);
    assert(simulation.read_signal(*done).to_msb_string() == "1");
    assert(std::filesystem::exists(directory / "vhdl-output.txt"));
    assert(read_text(directory / "vhdl-output.txt") == "27\n");
    assert(
        read_text(directory / "vhdl-text-output.txt")
        == "   7TRUE0  |\nX\n");
    assert(std::ranges::any_of(
        points,
        [&](const auto& point) {
            return fsim::test::same_source_path(point.source.path, source)
                && point.scope.find("vhdl_file_top") != std::string::npos;
        }));
#if defined(FSIM_HAS_LLVM)
    assert(
        engine == fsim::app::SimulationEngine::interpreter
            ? simulation.compiled_process_count() == 0
            : simulation.compiled_process_count() == 1);
#endif
    return cache;
}

} // namespace

int main()
{
    const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-sv-files-" + std::to_string(serial))
    };
    std::filesystem::create_directories(directory.path);
    const auto stable = directory.path / "stable.sv";
    const auto source = directory.path / "file.sv";
    const auto input = directory.path / "input.txt";
    const auto scan_input = directory.path / "scan.txt";
    const auto binary_input = directory.path / "binary.bin";
    const auto scalar_binary_input = directory.path / "scalar.bin";
    const auto packet_input = directory.path / "packets.hex";
    const auto vhdl_source = directory.path / "vhdl_file.vhd";
    write_text(
        stable,
        R"(
module stable_child;
  initial begin end
endmodule
)");
    write_text(
        vhdl_source,
        // FSIM-CONFORMANCE CF-VHDL-FILE-001 source=SRC-IEEE-P1076 expectation=execute
        // FSIM-CONFORMANCE CF-VHDL-TEXTIO-001 source=SRC-UVVM expectation=execute
        R"(
entity vhdl_file_top is end entity;
architecture rtl of vhdl_file_top is
  type integer_file is file of integer;
  type text_file is file of string;
  signal done : bit;
begin
  worker: process
    file input_file : integer_file open read_mode is "vhdl-input.txt";
    file empty_file : integer_file open read_mode is "vhdl-empty.txt";
    file output_file : integer_file;
    file text_input : text_file open read_mode is "vhdl-text-input.txt";
    file text_output : text_file open write_mode is "vhdl-text-output.txt";
    variable status : file_open_status;
    variable value : integer;
    variable flag : boolean;
    variable digit : bit;
    variable good : boolean;
    variable input_line, output_line : line;
    procedure close_alias(file target : integer_file) is
    begin
      file_close(target);
    end procedure;
  begin
    assert not endfile(input_file)
      report "nonempty file reported endfile" severity failure;
    read(input_file, value);
    assert value = 17 report "direct file read" severity failure;
    assert endfile(input_file)
      report "consumed file did not report endfile" severity failure;
    assert endfile(empty_file)
      report "empty file did not report endfile" severity failure;
    readline(text_input, input_line);
    read(input_line, value, good);
    assert good and value = 42 report "TextIO integer read" severity failure;
    read(input_line, flag);
    assert flag report "TextIO boolean read" severity failure;
    read(input_line, digit);
    assert digit = '0' report "TextIO bit read" severity failure;
    read(input_line, value, good);
    assert not good and value = 42
      report "TextIO good failure" severity failure;
    write(output_line, 7, right, 4);
    write(output_line, true);
    write(output_line, digit, left, 3);
    write(output_line, "|");
    writeline(text_output, output_line);
    write(output_line, "X");
    writeline(text_output, output_line);
    file_open(status, output_file, "vhdl-output.txt", write_mode);
    assert status = open_ok report "open status" severity failure;
    write(output_file, 27);
    file_open(status, output_file, "vhdl-output.txt", append_mode);
    assert status = status_error report "already-open status" severity failure;
    close_alias(output_file);
    file_open(status, output_file, "missing-input.txt", read_mode);
    assert status = name_error report "missing-name status" severity failure;
    file_close(input_file);
    file_close(empty_file);
    file_close(text_input);
    file_close(text_output);
    done <= '1';
    wait;
  end process;
end architecture;

entity vhdl_textio_failure is end entity;
architecture rtl of vhdl_textio_failure is
  type text_file is file of string;
begin
  worker: process
    file input_file : text_file open read_mode is "vhdl-bad-text-input.txt";
    variable input_line : line;
    variable value : integer;
  begin
    readline(input_file, input_line);
    read(input_line, value);
    wait;
  end process;
end architecture;
)");
    // FSIM-CONFORMANCE CF-VHDL-TEXTIO-N01 source=SRC-UVVM expectation=runtime-failure
    write_text(directory.path / "vhdl-input.txt", "17");
    write_text(directory.path / "vhdl-empty.txt", "");
    write_text(
        directory.path / "vhdl-text-input.txt",
        "  42 TRUE 0 bad\n");
    write_text(directory.path / "vhdl-bad-text-input.txt", "bad\n");
    const auto write_source =
        [&](const std::string_view label) {
            std::ofstream output(
                source, std::ios::binary | std::ios::trunc);
            output
                << "`timescale 1ns/1ns\n"
                << "module file_top;\n"
                << "  stable_child stable();\n"
                << "  integer writer;\n"
                << "  integer monitor_writer;\n"
                << "  integer multichannel_first;\n"
                << "  integer multichannel_second;\n"
                << "  integer multichannel;\n"
                << "  integer reader;\n"
                << "  integer count;\n"
                << "  integer eof_status;\n"
                << "  integer eof_after_pushback;\n"
                << "  integer first_character;\n"
                << "  integer second_character;\n"
                << "  integer pushback_status;\n"
                << "  integer injected_status;\n"
                << "  integer eof_character;\n"
                << "  integer error_status;\n"
                << "  integer scan_reader;\n"
                << "  integer scan_count;\n"
                << "  integer scan_value;\n"
                << "  integer string_scan_count;\n"
                << "  integer scalar_scan_count;\n"
                << "  integer wide_scan_count;\n"
                << "  integer scan_hex;\n"
                << "  integer binary_reader;\n"
                << "  integer packed_count;\n"
                << "  integer memory_count;\n"
                << "  integer packet_count;\n"
                << "  integer binary_real_count;\n"
                << "  integer binary_time_count;\n"
                << "  integer binary_handle_count;\n"
                << "  integer position;\n"
                << "  integer seek_status;\n"
                << "  integer positioned_count;\n"
                << "  integer rewind_status;\n"
                << "  logic [31:0] binary_word;\n"
                << "  logic [15:0] positioned_word;\n"
                << "  logic [7:0] binary_memory [3:0];\n"
                << "  typedef struct packed {\n"
                << "    logic [3:0] tag;\n"
                << "    logic [3:0] data;\n"
                << "  } packet_t;\n"
                << "  packet_t packet_memory [1:0];\n"
                << "  logic [7:0] multidimensional_memory [1:0][0:1];\n"
                << "  string string_memory [0:1];\n"
                << "  typedef struct {\n"
                << "    logic [3:0] tag;\n"
                << "    logic [3:0] data;\n"
                << "  } aggregate_t;\n"
                << "  aggregate_t aggregate_memory [0:1];\n"
                << "  real scan_real_value;\n"
                << "  time scan_time_value;\n"
                << "  chandle scan_handle_value;\n"
                << "  real binary_real_value;\n"
                << "  time binary_time_value;\n"
                << "  chandle binary_handle_value;\n"
                << "  logic [128:0] wide_scan_value;\n"
                << "  logic [7:0] strobe_value;\n"
                << "  logic [3:0] strobe_binary;\n"
                << "  logic [7:0] monitor_value;\n"
                << "  string line;\n"
                << "  string saved;\n"
                << "  string error;\n"
                << "  string scan_word;\n"
                << "  string string_word;\n"
                << "  string scalar_formatted;\n"
                << "  string wide_formatted;\n"
                << "  task automatic read_one(\n"
                << "      input integer file_handle,\n"
                << "      output string value,\n"
                << "      output integer result);\n"
                << "    #1;\n"
                << "    result = $fgets(value, file_handle);\n"
                << "  endtask\n"
                << "  initial begin : worker\n"
                << "    writer = $fopen(\"output.txt\", \"w\");\n"
                << "    $fdisplay(writer, \"" << label
                << "=%0d\", 7);\n"
                << "    $fwrite(writer, \"%s\", \"tail\");\n"
                << "    $fdisplayb(writer, 4'b0111);\n"
                << "    $fdisplayh(writer, 8'ha5);\n"
                << "    $fdisplayo(writer, 8'ha5);\n"
                << "    $fwriteb(writer, 4'b0111);\n"
                << "    $fwriteh(writer, 8'ha5);\n"
                << "    $fwriteo(writer, 8'ha5);\n"
                << "    $fdisplay(writer, \"\");\n"
                << "    strobe_value = 8'h11;\n"
                << "    strobe_binary = 4'b0011;\n"
                << "    $fstrobe(writer, \"strobe=%m/%0t/%0h\", $time, strobe_value);\n"
                << "    $fstrobeb(writer, strobe_binary);\n"
                << "    $fstrobeh(writer, strobe_value);\n"
                << "    $fstrobeo(writer, strobe_value);\n"
                << "    strobe_value <= 8'ha5;\n"
                << "    strobe_binary <= 4'b1010;\n"
                << "    multichannel_first = $fopen(\"multichannel-first.txt\");\n"
                << "    multichannel_second = $fopen(\"multichannel-second.txt\");\n"
                << "    multichannel = multichannel_first | multichannel_second;\n"
                << "    $fdisplay(multichannel, \"shared=%0d\", 9);\n"
                << "    $fclose(multichannel);\n"
                << "    reader = $fopen(\"input.txt\", \"r\");\n"
                << "    first_character = $fgetc(reader);\n"
                << "    pushback_status = $ungetc(first_character, reader);\n"
                << "    second_character = $fgetc(reader);\n"
                << "    injected_status = $ungetc(8'h5a, reader);\n"
                << "    read_one(reader, line, count);\n"
                << "    saved = line;\n"
                << "    count = $fgets(line, reader);\n"
                << "    eof_status = $feof(reader);\n"
                << "    eof_character = $fgetc(reader);\n"
                << "    pushback_status = $ungetc(8'h51, reader);\n"
                << "    eof_after_pushback = $feof(reader);\n"
                << "    second_character = $fgetc(reader);\n"
                << "    eof_character = $fgetc(reader);\n"
                << "    eof_status = $feof(reader);\n"
                << "    error_status = $ferror(reader, error);\n"
                << "    $fclose(reader);\n"
                << "    scan_reader = $fopen(\"scan.txt\", \"r\");\n"
                << "    scan_count = $fscanf(scan_reader, \"%d %s\", scan_value, scan_word);\n"
                << "    $fclose(scan_reader);\n"
                << "    string_scan_count = $sscanf(\"h=2a word=done\", \"h=%h word=%s\", scan_hex, string_word);\n"
                << "    scalar_scan_count = $sscanf(\"real=1.25 time=17 handle=0\", \"real=%g time=%d handle=%h\", scan_real_value, scan_time_value, scan_handle_value);\n"
                << "    scalar_formatted = $sformatf(\"formatted=%g/%d/%0h\", scan_real_value, scan_time_value, scan_handle_value);\n"
                << "    wide_scan_count = $sscanf(\"10x23456789abcdefz123456789abcdef\", \"%h\", wide_scan_value);\n"
                << "    wide_formatted = $sformatf(\"wide=%0h\", wide_scan_value);\n"
                << "    binary_reader = $fopen(\"binary.bin\", \"rb\");\n"
                << "    packed_count = $fread(binary_word, binary_reader);\n"
                << "    position = $ftell(binary_reader);\n"
                << "    seek_status = $fseek(binary_reader, "
                   "129'h1fffffffffffffffffffffffffffffffe, 1);\n"
                << "    positioned_count = $fread(positioned_word, binary_reader);\n"
                << "    rewind_status = $rewind(binary_reader);\n"
                << "    memory_count = $fread(binary_memory, binary_reader, 2, 2);\n"
                << "    rewind_status = $rewind(binary_reader);\n"
                << "    packet_count = $fread(packet_memory, binary_reader, 1, 2);\n"
                << "    rewind_status = $rewind(binary_reader);\n"
                << "    memory_count = $fread(multidimensional_memory, binary_reader, 1, 2);\n"
                << "    $writememh(\"multidimensional-fread.hex\", multidimensional_memory);\n"
                << "    rewind_status = $rewind(binary_reader);\n"
                << "    packet_count = $fread(aggregate_memory, binary_reader, 0, 2);\n"
                << "    $writememh(\"aggregates-fread.hex\", aggregate_memory);\n"
                << "    $fclose(binary_reader);\n"
                << "    binary_reader = $fopen(\"scalar.bin\", \"rb\");\n"
                << "    binary_real_count = $fread(binary_real_value, binary_reader);\n"
                << "    binary_time_count = $fread(binary_time_value, binary_reader);\n"
                << "    binary_handle_count = $fread(binary_handle_value, binary_reader);\n"
                << "    $fclose(binary_reader);\n"
                << "    $writememh(\"dump.hex\", binary_memory);\n"
                << "    $writememb(\"dump.bin\", binary_memory, 2, 1);\n"
                << "    $writememh(\"packets-fread.hex\", packet_memory);\n"
                << "    $readmemh(\"packets.hex\", packet_memory);\n"
                << "    $writememh(\"packets-dump.hex\", packet_memory);\n"
                << "    $readmemh(\"multidimensional.hex\", multidimensional_memory);\n"
                << "    $writememh(\"multidimensional-dump.hex\", multidimensional_memory);\n"
                << "    $readmemh(\"strings.mem\", string_memory);\n"
                << "    $writememb(\"strings-dump.mem\", string_memory);\n"
                << "    $readmemh(\"aggregates.hex\", aggregate_memory);\n"
                << "    $writememh(\"aggregates-dump.hex\", aggregate_memory);\n"
                << "    $fwrite(writer, \"|chars=%0d\", first_character);\n"
                << "    $fwrite(writer, \"/%0d\", pushback_status);\n"
                << "    $fwrite(writer, \"/%0d\", second_character);\n"
                << "    $fwrite(writer, \"/%0d\", injected_status);\n"
                << "    $fwrite(writer, \"/%0d\", eof_after_pushback);\n"
                << "    $fwrite(writer, \"/%0d\", eof_character);\n"
                << "    $fdisplay(writer, \"/%0d\", eof_status);\n"
                << "    $fwrite(writer, \"scan=%0d\", scan_count);\n"
                << "    $fwrite(writer, \"/%0d\", scan_value);\n"
                << "    $fwrite(writer, \"/%s\", scan_word);\n"
                << "    $fwrite(writer, \"/%0d\", string_scan_count);\n"
                << "    $fwrite(writer, \"/%0d\", scan_hex);\n"
                << "    $fdisplay(writer, \"/%s\", string_word);\n"
                << "    $fwrite(writer, \"scalar=%g\", scan_real_value);\n"
                << "    $fwrite(writer, \"/%0t\", scan_time_value);\n"
                << "    $fwrite(writer, \"/%0h\", scan_handle_value);\n"
                << "    $fdisplay(writer, \"/%0d\", scalar_scan_count);\n"
                << "    $fdisplay(writer, \"%s\", scalar_formatted);\n"
                << "    $fdisplay(writer, \"%s/%0d\", wide_formatted, wide_scan_count);\n"
                << "    $fdisplay(writer, \"wide-direct=%0h\", wide_scan_value);\n"
                << "    $fwrite(writer, \"binary=%0d\", packed_count);\n"
                << "    $fwrite(writer, \"/%0d\", binary_word);\n"
                << "    $fwrite(writer, \"/%0d\", position);\n"
                << "    $fwrite(writer, \"/%0d\", seek_status);\n"
                << "    $fwrite(writer, \"/%0d\", positioned_count);\n"
                << "    $fwrite(writer, \"/%0d\", positioned_word);\n"
                << "    $fwrite(writer, \"/%0d\", rewind_status);\n"
                << "    $fwrite(writer, \"/%0d\", memory_count);\n"
                << "    $fwrite(writer, \"/%0d\", binary_memory[2]);\n"
                << "    $fdisplay(writer, \"/%0d\", binary_memory[1]);\n"
                << "    $fwrite(writer, \"binary_scalar=%g\", binary_real_value);\n"
                << "    $fwrite(writer, \"/%0t\", binary_time_value);\n"
                << "    $fwrite(writer, \"/%0h\", binary_handle_value);\n"
                << "    $fwrite(writer, \"/%0d\", binary_real_count);\n"
                << "    $fwrite(writer, \"/%0d\", binary_time_count);\n"
                << "    $fdisplay(writer, \"/%0d\", binary_handle_count);\n"
                << "    $fflush(writer);\n"
                << "    $fflush();\n"
                << "    $fclose(writer);\n"
                << "  end\n"
                << "  initial begin : file_monitor\n"
                << "    monitor_writer = $fopen(\"monitor.txt\", \"w\");\n"
                << "    monitor_value = 8'h11;\n"
                << "    $fmonitorb(monitor_writer, monitor_value);\n"
                << "    monitor_value <= 8'ha5;\n"
                << "    #1;\n"
                << "    $fmonitorh(monitor_writer, monitor_value);\n"
                << "    monitor_value <= 8'h3c;\n"
                << "    #1;\n"
                << "    $fmonitoro(monitor_writer, monitor_value);\n"
                << "    monitor_value <= 8'h17;\n"
                << "    #1;\n"
                << "    $fmonitor(monitor_writer, \"decimal=%0d\", monitor_value);\n"
                << "    monitor_value <= 8'h09;\n"
                << "    #1;\n"
                << "    $fclose(monitor_writer);\n"
                << "  end\n"
                << "endmodule\n";
            assert(output.good());
        };

    for (const auto optimization :
        { fsim::project::Optimization::o0,
            fsim::project::Optimization::o2 }) {
        write_source("value");
        write_text(input, "alpha\n");
        write_text(scan_input, "13 alpha\n");
        write_text(binary_input, std::string { "\x12\x34\x56\x78\x9a\xbc", 6 });
        write_text(
            scalar_binary_input,
            std::string {
                "\x3f\xf4\x00\x00\x00\x00\x00\x00"
                "\x00\x00\x00\x00\x00\x00\x00\x17"
                "\x00\x00\x00\x00\x00\x00\x00\x00",
                24 });
        write_text(packet_input, "a1\nb2\n");
        write_text(
            directory.path / "multidimensional.hex",
            "11\n22\n33\n44\n");
        write_text(
            directory.path / "strings.mem",
            "\"alpha beta\"\n\"line\\n//literal\"\n");
        write_text(
            directory.path / "aggregates.hex",
            "a5\n3c\n");
        const auto config = make_config(directory.path, stable, source, optimization);
        const auto reference = run_once(config, fsim::app::SimulationEngine::interpreter);
        const auto cold = run_once(config, fsim::app::SimulationEngine::compiled);
        const auto warm = run_once(config, fsim::app::SimulationEngine::compiled);
        assert(reference.result.status
            == fsim::runtime::RunStatus::completed);
        assert(reference.result.time == 4);
        assert(reference.output_file
            == "value=7\ntail0111\na5\n245\n0111a5245\n"
               "strobe=file_top/0/a5\n1010\na5\n245\n"
               "|chars=97/81/81/90/0/-1/1\n"
               "scan=2/13/alpha/2/42/done\n"
               "scalar=1.25/17/0/3\n"
               "formatted=1.25/17/0\n"
               "wide=10x23456789abcdefz123456789abcdef/1\n"
               "wide-direct=10x23456789abcdefz123456789abcdef\n"
               "binary=4/305419896/4/0/2/22136/0/2/18/52\n"
               "binary_scalar=1.25/23/0/8/8/8\n");
        assert(reference.monitor_file
            == "10100101\n3c\n027\ndecimal=9\n");
        assert(reference.saved_line == "Zlpha\n");
        assert(reference.multichannel_first == "shared=9\n");
        assert(reference.multichannel_second == "shared=9\n");
        assert(reference.binary_word
            == "00010010001101000101011001111000");
        assert(reference.positioned_word == "0101011001111000");
        assert((reference.binary_memory
            == std::vector<std::string> {
                "XXXXXXXX", "00010010", "00110100", "XXXXXXXX" }));
        assert(reference.memory_hex_dump == "xx\n34\n12\nxx\n");
        assert(
            reference.memory_binary_dump
            == "00010010\n00110100\n");
        assert(reference.packet_fread_dump == "34\n12\n");
        assert(reference.packet_memory_dump == "a1\nb2\n");
        assert(
            reference.multidimensional_memory_dump
            == "11\n22\n33\n44\n");
        assert(
            reference.string_memory_dump
            == "\"alpha beta\"\n\"line\\n//literal\"\n");
        assert(reference.aggregate_memory_dump == "a5\n3c\n");
        assert(
            reference.multidimensional_fread_dump
            == "xx\n12\n34\nxx\n");
        assert(reference.aggregate_fread_dump == "12\n34\n");
        assert((reference.packet_memory
            == std::vector<std::string> { "10110010", "10100001" }));
        assert(reference.output_file == cold.output_file);
        assert(reference.monitor_file == cold.monitor_file);
        assert(reference.multichannel_first == cold.multichannel_first);
        assert(reference.multichannel_second == cold.multichannel_second);
        assert(reference.saved_line == cold.saved_line);
        assert(reference.binary_word == cold.binary_word);
        assert(reference.positioned_word == cold.positioned_word);
        assert(reference.binary_memory == cold.binary_memory);
        assert(reference.memory_hex_dump == cold.memory_hex_dump);
        assert(reference.memory_binary_dump == cold.memory_binary_dump);
        assert(reference.packet_fread_dump == cold.packet_fread_dump);
        assert(reference.packet_memory_dump == cold.packet_memory_dump);
        assert(
            reference.multidimensional_memory_dump
            == cold.multidimensional_memory_dump);
        assert(reference.string_memory_dump == cold.string_memory_dump);
        assert(reference.aggregate_memory_dump == cold.aggregate_memory_dump);
        assert(
            reference.multidimensional_fread_dump
            == cold.multidimensional_fread_dump);
        assert(reference.aggregate_fread_dump == cold.aggregate_fread_dump);
        assert(reference.packet_memory == cold.packet_memory);
        assert(cold.output_file == warm.output_file);
        assert(cold.monitor_file == warm.monitor_file);
        assert(cold.multichannel_first == warm.multichannel_first);
        assert(cold.multichannel_second == warm.multichannel_second);
        assert(cold.saved_line == warm.saved_line);
        assert(cold.binary_word == warm.binary_word);
        assert(cold.positioned_word == warm.positioned_word);
        assert(cold.binary_memory == warm.binary_memory);
        assert(cold.memory_hex_dump == warm.memory_hex_dump);
        assert(cold.memory_binary_dump == warm.memory_binary_dump);
        assert(cold.packet_fread_dump == warm.packet_fread_dump);
        assert(cold.packet_memory_dump == warm.packet_memory_dump);
        assert(
            cold.multidimensional_memory_dump
            == warm.multidimensional_memory_dump);
        assert(cold.string_memory_dump == warm.string_memory_dump);
        assert(cold.aggregate_memory_dump == warm.aggregate_memory_dump);
        assert(
            cold.multidimensional_fread_dump
            == warm.multidimensional_fread_dump);
        assert(cold.aggregate_fread_dump == warm.aggregate_fread_dump);
        assert(cold.packet_memory == warm.packet_memory);
        assert(reference.keys == cold.keys && cold.keys == warm.keys);
        assert(
            std::ranges::count_if(
                reference.points,
                [](const auto& point) {
                    return point.kind
                        == fsim::runtime::simir::
                            ExecutionPointKind::call;
                })
            >= 7);
#if defined(FSIM_HAS_LLVM)
        assert(cold.compiled_processes == 3);
        assert(cold.cache.misses == 2 && cold.cache.stores == 2);
        assert(warm.cache.hits == 2);
#endif
        verify_suspension(
            config, fsim::app::SimulationEngine::interpreter);
        verify_suspension(
            config, fsim::app::SimulationEngine::compiled);

        write_text(input, "beta\n");
        write_text(scan_input, "21 beta\n");
        write_text(binary_input, std::string { "\x21\x43\x65\x87\xab\xcd", 6 });
        write_text(packet_input, "c3\nd4\n");
        const auto changed_input = run_once(config, fsim::app::SimulationEngine::compiled);
        assert(changed_input.saved_line == "Zeta\n");
        assert(changed_input.output_file
            == "value=7\ntail0111\na5\n245\n0111a5245\n"
               "strobe=file_top/0/a5\n1010\na5\n245\n"
               "|chars=98/81/81/90/0/-1/1\n"
               "scan=2/21/beta/2/42/done\n"
               "scalar=1.25/17/0/3\n"
               "formatted=1.25/17/0\n"
               "wide=10x23456789abcdefz123456789abcdef/1\n"
               "wide-direct=10x23456789abcdefz123456789abcdef\n"
               "binary=4/558065031/4/0/2/25991/0/2/33/67\n"
               "binary_scalar=1.25/23/0/8/8/8\n");
        assert(changed_input.monitor_file == reference.monitor_file);
        assert(changed_input.binary_word
            == "00100001010000110110010110000111");
        assert(changed_input.positioned_word == "0110010110000111");
        assert((changed_input.binary_memory
            == std::vector<std::string> {
                "XXXXXXXX", "00100001", "01000011", "XXXXXXXX" }));
        assert(changed_input.memory_hex_dump == "xx\n43\n21\nxx\n");
        assert(
            changed_input.memory_binary_dump
            == "00100001\n01000011\n");
        assert(changed_input.packet_fread_dump == "43\n21\n");
        assert(changed_input.packet_memory_dump == "c3\nd4\n");
        assert((changed_input.packet_memory
            == std::vector<std::string> { "11010100", "11000011" }));
        assert(changed_input.keys == warm.keys);
#if defined(FSIM_HAS_LLVM)
        assert(changed_input.cache.hits == 2);
#endif

        write_source("changed");
        const auto changed_source = run_once(config, fsim::app::SimulationEngine::compiled);
        assert(changed_source.output_file
            == "changed=7\ntail0111\na5\n245\n0111a5245\n"
               "strobe=file_top/0/a5\n1010\na5\n245\n"
               "|chars=98/81/81/90/0/-1/1\n"
               "scan=2/21/beta/2/42/done\n"
               "scalar=1.25/17/0/3\n"
               "formatted=1.25/17/0\n"
               "wide=10x23456789abcdefz123456789abcdef/1\n"
               "wide-direct=10x23456789abcdefz123456789abcdef\n"
               "binary=4/558065031/4/0/2/25991/0/2/33/67\n"
               "binary_scalar=1.25/23/0/8/8/8\n");
        assert(changed_source.keys != changed_input.keys);
#if defined(FSIM_HAS_LLVM)
        assert(changed_source.cache.hits == 1);
        assert(changed_source.cache.misses == 1);
        assert(changed_source.cache.stores == 1);
#endif
        verify_fstrobe_expression_diagnostic(config, source);
        verify_bad_path(
            config, source, fsim::app::SimulationEngine::interpreter);
        verify_bad_path(
            config, source, fsim::app::SimulationEngine::compiled);
        static_cast<void>(verify_vhdl_file_objects(
            directory.path, vhdl_source, optimization,
            fsim::app::SimulationEngine::interpreter));
        const auto vhdl_cold = verify_vhdl_file_objects(
            directory.path, vhdl_source, optimization,
            fsim::app::SimulationEngine::compiled);
        const auto vhdl_warm = verify_vhdl_file_objects(
            directory.path, vhdl_source, optimization,
            fsim::app::SimulationEngine::compiled);
#if defined(FSIM_HAS_LLVM)
        assert(vhdl_cold.hits == 0);
        assert(vhdl_cold.misses == 1);
        assert(vhdl_cold.stores == 1);
        assert(vhdl_warm.hits == 1);
        assert(vhdl_warm.misses == 0);
#else
        static_cast<void>(vhdl_cold);
        static_cast<void>(vhdl_warm);
#endif
        verify_vhdl_textio_failure(
            directory.path, vhdl_source, optimization,
            fsim::app::SimulationEngine::interpreter);
        verify_vhdl_textio_failure(
            directory.path, vhdl_source, optimization,
            fsim::app::SimulationEngine::compiled);
    }

    const auto wide_source = directory.path / "wide-file.sv";
    write_text(
        wide_source,
        R"(
module wide_file_top;
  integer reader;
  integer packed_count;
  integer memory_count;
  integer rewind_status;
  logic [136:0] debug_value;
  logic [136:0] packed_word;
  logic [136:0] wide_memory [1:0];
  initial begin
    reader = $fopen("wide.bin", "rb");
    packed_count = $fread(packed_word, reader);
    rewind_status = $rewind(reader);
    memory_count = $fread(wide_memory, reader, 1, 2);
    $fclose(reader);
    $writememh("wide-fread.hex", wide_memory);
    $readmemh("wide-read.hex", wide_memory);
    $writememb("wide-read.bin", wide_memory);
    #1 $finish;
  end
endmodule
)");
    std::string wide_binary;
    const std::array<std::uint8_t, 18> wide_seed_bytes {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08
    };
    const std::array<std::uint8_t, 18> wide_amount_bytes {
        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81
    };
    for (const auto byte : wide_seed_bytes) {
        wide_binary.push_back(static_cast<char>(byte));
    }
    for (const auto byte : wide_amount_bytes) {
        wide_binary.push_back(static_cast<char>(byte));
    }
    write_text(directory.path / "wide.bin", wide_binary);
    std::string seed_hex(35, '0');
    seed_hex[0] = '1';
    seed_hex[16] = '2';
    seed_hex[34] = '8';
    std::string amount_hex(35, '0');
    amount_hex[2] = '1';
    amount_hex[33] = '8';
    amount_hex[34] = '1';
    write_text(
        directory.path / "wide-read.hex",
        seed_hex + "\n" + amount_hex + "\n");
    auto wide_config = make_config(
        directory.path, stable, wide_source,
        fsim::project::Optimization::o0);
    wide_config.project.name = "sv-wide-files";
    wide_config.project.top = "sv:work.wide_file_top";
    wide_config.build.cache_path = directory.path / "wide-file-cache";
    fsim::diagnostic::Engine wide_diagnostics;
    auto wide_project = fsim::app::build_project(
        wide_config, wide_diagnostics);
    if (!wide_project) {
        for (const auto& diagnostic : wide_diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(wide_project);
    fsim::app::Simulation wide_simulation {
        std::move(*wide_project), wide_config.run.max_deltas,
        fsim::app::SimulationEngine::interpreter
    };
    const auto debug_value = wide_simulation.find_signal(
        "wide_file_top.debug_value");
    assert(debug_value);
    auto wide_seed = fsim::runtime::PackedLogic4 {
        137, fsim::runtime::Logic4::zero
    };
    wide_seed.set(136, fsim::runtime::Logic4::one);
    wide_seed.set(73, fsim::runtime::Logic4::one);
    wide_seed.set(3, fsim::runtime::Logic4::one);
    auto wide_amount = fsim::runtime::PackedLogic4 {
        137, fsim::runtime::Logic4::zero
    };
    wide_amount.set(128, fsim::runtime::Logic4::one);
    wide_amount.set(7, fsim::runtime::Logic4::one);
    wide_amount.set(0, fsim::runtime::Logic4::one);
    std::vector<fsim::runtime::PackedLogic4> callback_values;
    std::ostringstream vcd_output;
    fsim::runtime::VcdWriter vcd { vcd_output, "1ns", 256 };
    const auto trace = vcd.declare_signal(
        "wide_file_top.debug_value", 137);
    vcd.begin(wide_simulation.now());
    wide_simulation.set_signal_change_hook(
        [&](const auto signal, const auto& value, const auto time, const auto) {
            if (signal == *debug_value) {
                callback_values.push_back(value);
                vcd.set_time(time);
                vcd.change(trace, value);
            }
        });
    std::ostringstream debug_output;
    std::ostringstream debug_error;
    fsim::app::DebuggerControl debugger {
        wide_simulation, debug_output, debug_error
    };
    debugger.execute({ "deposit", "debug_value", wide_seed.to_msb_string() });
    const auto snapshot = wide_simulation.read_signal(*debug_value);
    debugger.execute({ "show", "debug_value" });
    debugger.execute({ "force", "debug_value", wide_amount.to_msb_string() });
    debugger.execute({ "show", "debug_value" });
    debugger.execute({ "deposit", "debug_value", wide_seed.to_msb_string() });
    debugger.execute({ "release", "debug_value" });
    debugger.execute({ "show", "debug_value" });
    assert(debug_error.str().empty());
    assert(snapshot == wide_seed);
    assert(wide_simulation.read_signal(*debug_value) == wide_seed);
    assert(debug_output.str().find(wide_seed.to_msb_string())
        != std::string::npos);
    assert(debug_output.str().find(
               wide_amount.to_msb_string() + " (forced)")
        != std::string::npos);
    const auto wide_result = wide_simulation.run();
    assert(wide_result.status == fsim::runtime::RunStatus::stopped);
    assert(wide_result.time == 1);
    vcd.flush();
    assert(std::ranges::find(callback_values, wide_seed)
        != callback_values.end());
    assert(std::ranges::find(callback_values, wide_amount)
        != callback_values.end());
    assert(vcd_output.str().find(wide_seed.to_msb_string())
        != std::string::npos);
    assert(vcd_output.str().find(wide_amount.to_msb_string())
        != std::string::npos);
    const auto packed_word = wide_simulation.find_signal(
        "wide_file_top.packed_word");
    const auto wide_memory = wide_simulation.design().find_container(
        "wide_file_top.wide_memory");
    assert(packed_word && wide_memory);
    assert(wide_simulation.read_signal(*packed_word) == wide_seed);
    const auto memory = wide_simulation.read_container_object(*wide_memory);
    assert(memory.elements.size() == 2);
    assert(memory.elements[0] == wide_amount);
    assert(memory.elements[1] == wide_seed);
    assert(read_text(directory.path / "wide-fread.hex")
        == amount_hex + "\n" + seed_hex + "\n");
    assert(read_text(directory.path / "wide-read.bin")
        == wide_seed.to_msb_string() + "\n"
            + wide_amount.to_msb_string() + "\n");

    const auto run_wide_compiled = [&](
                                       const fsim::project::Optimization optimization) {
        auto compiled_config = make_config(
            directory.path, stable, wide_source, optimization);
        compiled_config.project.name = "sv-wide-files";
        compiled_config.project.top = "sv:work.wide_file_top";
        compiled_config.build.cache_path = directory.path
            / (optimization == fsim::project::Optimization::o0
                    ? "wide-file-compiled-o0"
                    : "wide-file-compiled-o2");
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(
            compiled_config, diagnostics);
        if (!project) {
            for (const auto& diagnostic : diagnostics.diagnostics()) {
                std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
            }
        }
        assert(project);
        fsim::app::Simulation simulation {
            std::move(*project), compiled_config.run.max_deltas,
            fsim::app::SimulationEngine::compiled
        };
#if defined(FSIM_HAS_LLVM)
        assert(simulation.compiled_process_count() == 1);
#endif
        assert(simulation.run().status == fsim::runtime::RunStatus::stopped);
        const auto packed = simulation.find_signal(
            "wide_file_top.packed_word");
        const auto container = simulation.design().find_container(
            "wide_file_top.wide_memory");
        assert(packed && container);
        assert(simulation.read_signal(*packed) == wide_seed);
        const auto values = simulation.read_container_object(*container);
        assert(values.elements.size() == 2);
        assert(values.elements[0] == wide_amount);
        assert(values.elements[1] == wide_seed);
        assert(read_text(directory.path / "wide-fread.hex")
            == amount_hex + "\n" + seed_hex + "\n");
        assert(read_text(directory.path / "wide-read.bin")
            == wide_seed.to_msb_string() + "\n"
                + wide_amount.to_msb_string() + "\n");
        return simulation.native_cache_statistics();
    };
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto cold = run_wide_compiled(optimization);
        const auto warm = run_wide_compiled(optimization);
#if defined(FSIM_HAS_LLVM)
        assert(cold.hits == 0 && cold.misses == 1 && cold.stores == 1);
        assert(warm.hits == 1 && warm.misses == 0);
#else
        static_cast<void>(cold);
        static_cast<void>(warm);
#endif
    }

    const auto verilog_source = directory.path / "verilog-wide-memory.v";
    const auto verilog_input = directory.path / "verilog-wide-memory.bin";
    const auto verilog_output = directory.path / "verilog-wide-memory-out.bin";
    const auto verilog_line_input = directory.path / "verilog-line.txt";
    const auto verilog_text_output = directory.path / "verilog-output.txt";
    auto wide_unknown = fsim::runtime::PackedLogic4 {
        137, fsim::runtime::Logic4::zero
    };
    wide_unknown.set(136, fsim::runtime::Logic4::x);
    wide_unknown.set(72, fsim::runtime::Logic4::z);
    wide_unknown.set(0, fsim::runtime::Logic4::one);
    write_text(
        verilog_input,
        wide_seed.to_msb_string() + "\n"
            + wide_amount.to_msb_string() + "\n"
            + wide_unknown.to_msb_string() + "\n");
    const std::string verilog_line { "Verilog-2005 packed line\n" };
    write_text(verilog_line_input, verilog_line);
    {
        std::ofstream output(verilog_source, std::ios::binary);
        output
            << "module verilog_memory_word_port(\n"
            << "    input [136:0] source, output [136:0] destination);\n"
            << "  assign destination = source;\n"
            << "endmodule\n"
            << "module verilog_memory_top;\n"
            << "  reg [136:0] memory [3:0];\n"
            << "  wire [136:0] selected_word;\n"
            << "  reg [136:0] line;\n"
            << "  reg [136:0] error_text;\n"
            << "  integer handle;\n"
            << "  integer status;\n"
            << "  verilog_memory_word_port word_port(\n"
            << "      .source(memory[2]), .destination(selected_word));\n"
            << "  initial begin\n"
            << "    $readmemb(\"verilog-wide-memory.bin\", memory, 3, 1);\n"
            << "    $writememb(\"verilog-wide-memory-out.bin\", memory, 3, 1);\n"
            << "    handle = $fopen(\"verilog-line.txt\", \"r\");\n"
            << "    status = $fgets(line, handle);\n"
            << "    status = $ferror(handle, error_text);\n"
            << "    $fclose(handle);\n"
            << "    handle = $fopen(\"verilog-output.txt\", \"w\");\n"
            << "    $fdisplay(handle, \"verilog-2005\");\n"
            << "    $fflush(handle);\n"
            << "    $fclose(handle);\n"
            << "    #1;\n"
            << "    memory[0] = selected_word;\n"
            << "    memory[0][136] = memory[3][136];\n"
            << "  end\n"
            << "endmodule\n";
        assert(output.good());
    }
    const auto run_verilog_memory = [&](
                                        const fsim::project::Optimization optimization,
                                        const fsim::app::SimulationEngine engine) {
        fsim::project::Config config;
        config.base_directory = directory.path;
        config.project.name = "verilog-wide-memory";
        config.project.top = "verilog:work.verilog_memory_top";
        config.project.time_resolution = "1ns";
        config.build.optimization = optimization;
        config.build.cache_path = directory.path
            / (optimization == fsim::project::Optimization::o0
                    ? "verilog-wide-memory-o0"
                    : "verilog-wide-memory-o2");
        config.run.max_deltas = 1000;
        fsim::project::SourceSet sources;
        sources.language = fsim::project::Language::verilog;
        sources.standard = "2005";
        sources.library = "work";
        sources.files = { verilog_source };
        config.source_sets.push_back(std::move(sources));
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        if (!project) {
            for (const auto& diagnostic : diagnostics.diagnostics()) {
                std::cerr << diagnostic.code << ": "
                          << diagnostic.message << '\n';
            }
        }
        assert(project);
        fsim::diagnostic::Engine artifact_diagnostics;
        const auto encoded = fsim::app::serialize_runtime_state(
            project->design, artifact_diagnostics);
        assert(encoded && !artifact_diagnostics.has_error());
        auto restored = fsim::app::deserialize_runtime_state(
            *encoded, "verilog-file-runtime", artifact_diagnostics);
        assert(restored && !artifact_diagnostics.has_error());
        assert(fsim::app::serialize_runtime_state(
                   *restored, artifact_diagnostics)
            == encoded);
        project->design = std::move(*restored);
        fsim::app::Simulation simulation {
            std::move(*project), config.run.max_deltas, engine
        };
        const auto cache = simulation.native_cache_statistics();
        auto& vpi = simulation.systemverilog_vpi_objects();
        const auto vpi_memory = vpi.find("verilog_memory_top.memory");
        const auto vpi_word_three
            = vpi.find("verilog_memory_top.memory[3]");
        const auto vpi_word_two
            = vpi.find("verilog_memory_top.memory[2]");
        assert(vpi_memory && vpi_word_three && vpi_word_two
            && vpi_memory.value->kind
                == fsim::runtime::SystemVerilogVpiObjectKind::Memory
            && vpi_word_three.value->kind
                == fsim::runtime::SystemVerilogVpiObjectKind::Variable
            && vpi_word_two.value->kind
                == fsim::runtime::SystemVerilogVpiObjectKind::Variable
            && vpi_word_three.value->parent == vpi_memory.value->handle
            && vpi_word_two.value->parent == vpi_memory.value->handle);
        assert(simulation.run().status
            == fsim::runtime::RunStatus::completed);
        const auto memory_id = simulation.design().find_container(
            "verilog_memory_top.memory");
        assert(memory_id);
        const auto value = simulation.read_container_object(*memory_id);
        assert(value.elements.size() == 4);
        assert(value.elements[0] == wide_seed);
        assert(value.elements[1] == wide_amount);
        assert(value.elements[2] == wide_unknown);
        auto selected_memory_word = wide_amount;
        selected_memory_word.set(136, fsim::runtime::Logic4::one);
        assert(value.elements[3] == selected_memory_word);
        const auto line_id = simulation.find_signal("verilog_memory_top.line");
        const auto error_id = simulation.find_signal(
            "verilog_memory_top.error_text");
        assert(line_id && error_id);
        assert(simulation.read_signal(*line_id)
            == fsim::runtime::simir::pack_file_text(verilog_line, 137));
        const auto empty_error = fsim::runtime::PackedLogic4 {
            137, fsim::runtime::Logic4::zero
        };
        assert(simulation.read_signal(*error_id) == empty_error);
        assert(read_text(verilog_output)
            == wide_seed.to_msb_string() + "\n"
                + wide_amount.to_msb_string() + "\n"
                + wide_unknown.to_msb_string() + "\n");
        assert(read_text(verilog_text_output) == "verilog-2005\n");
        return cache;
    };
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        static_cast<void>(run_verilog_memory(
            optimization, fsim::app::SimulationEngine::interpreter));
        const auto cold = run_verilog_memory(
            optimization, fsim::app::SimulationEngine::compiled);
        const auto warm = run_verilog_memory(
            optimization, fsim::app::SimulationEngine::compiled);
#if defined(FSIM_HAS_LLVM)
        assert(cold.hits == 0 && cold.misses == 1 && cold.stores == 1);
        assert(warm.hits == 1 && warm.misses == 0);
#else
        static_cast<void>(cold);
        static_cast<void>(warm);
#endif
    }
    return 0;
}
