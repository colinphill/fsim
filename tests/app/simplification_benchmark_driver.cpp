// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/runtime/vpi_data_read.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace {

constexpr std::string_view tb_pass_marker {
    "PASS: simplification mixed long cycles=100000 instances=16 "
    "expected_logical_changes=3200000"
};

constexpr std::string_view coverage_tb_pass_marker {
    "PASS: simplification pure SV coverage value=2 coverage=100.0"
};

struct Options {
    std::filesystem::path design;
    std::filesystem::path cache;
    std::filesystem::path file_root;
    std::filesystem::path coverage_path;
    std::string mode;
    std::string root;
    fsim::app::SimulationEngine engine {
        fsim::app::SimulationEngine::interpreter
    };
    fsim::project::Optimization optimization { fsim::project::Optimization::o2 };
    std::uint64_t seed { 1U };
    std::uint64_t max_deltas { 100'000'000U };
};

class Checksum {
public:
    void add(const std::uint64_t value)
    {
        for (unsigned shift = 0; shift < 64U; shift += 8U) {
            add(static_cast<unsigned char>(value >> shift));
        }
    }

    void add(const std::string_view value)
    {
        for (const auto character : value) {
            add(static_cast<unsigned char>(character));
        }
    }

    [[nodiscard]] std::uint64_t value() const noexcept { return value_; }

private:
    void add(const unsigned char value)
    {
        value_ ^= value;
        value_ *= UINT64_C(1099511628211);
    }

    std::uint64_t value_ { UINT64_C(14695981039346656037) };
};

[[nodiscard]] std::optional<std::uint64_t> parse_unsigned(
    const std::string_view text)
{
    if (text.empty()) {
        return std::nullopt;
    }
    for (const auto character : text) {
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
    }
    try {
        std::size_t consumed { };
        const auto value = std::stoull(std::string { text }, &consumed);
        if (consumed == text.size()) {
            return value;
        }
    } catch (...) {
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Options> parse_options(
    const int argc, char* argv[])
{
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument { argv[index] };
        if (argument == "--version") {
            std::cout << "fsim simplification benchmark driver 1\n";
            return std::nullopt;
        }
        if (index + 1 >= argc) {
            std::cerr << "missing value for " << argument << '\n';
            return std::nullopt;
        }
        const std::string_view value { argv[++index] };
        if (argument == "--design") {
            options.design = value;
        } else if (argument == "--cache") {
            options.cache = value;
        } else if (argument == "--file-root") {
            options.file_root = value;
        } else if (argument == "--coverage-path") {
            options.coverage_path = value;
        } else if (argument == "--mode") {
            options.mode = value;
        } else if (argument == "--root") {
            options.root = value;
        } else if (argument == "--engine") {
            if (value == "interpreter") {
                options.engine = fsim::app::SimulationEngine::interpreter;
            } else if (value == "compiled") {
                options.engine = fsim::app::SimulationEngine::compiled;
            } else {
                std::cerr << "unknown engine: " << value << '\n';
                return std::nullopt;
            }
        } else if (argument == "--optimization") {
            if (value == "O0") {
                options.optimization = fsim::project::Optimization::o0;
            } else if (value == "O2") {
                options.optimization = fsim::project::Optimization::o2;
            } else {
                std::cerr << "unsupported optimization: " << value << '\n';
                return std::nullopt;
            }
        } else if (argument == "--seed") {
            const auto parsed = parse_unsigned(value);
            if (!parsed) {
                std::cerr << "invalid seed: " << value << '\n';
                return std::nullopt;
            }
            options.seed = *parsed;
        } else if (argument == "--max-deltas") {
            const auto parsed = parse_unsigned(value);
            if (!parsed || *parsed == 0U) {
                std::cerr << "invalid max deltas: " << value << '\n';
                return std::nullopt;
            }
            options.max_deltas = *parsed;
        } else {
            std::cerr << "unknown option: " << argument << '\n';
            return std::nullopt;
        }
    }
    if (options.design.empty() || options.cache.empty() || options.root.empty()
        || (options.mode != "observer" && options.mode != "history"
            && options.mode != "coverage")) {
        std::cerr << "--design, --cache, --root, and --mode "
                     "observer|history|coverage are required\n";
        return std::nullopt;
    }
    if (options.mode == "coverage"
        && (options.file_root.empty() || options.coverage_path.empty()
            || options.coverage_path.is_absolute()
            || std::ranges::any_of(options.coverage_path,
                [](const auto& component) {
                    return component == std::filesystem::path { ".." };
                }))) {
        std::cerr << "coverage mode requires a relative --coverage-path "
                     "beneath --file-root\n";
        return std::nullopt;
    }
    return options;
}

[[nodiscard]] std::string packed_bits(
    const fsim::runtime::SystemVerilogVpiStoredValue& value)
{
    const auto* packed
        = std::get_if<fsim::runtime::PackedLogic4>(&value.payload);
    return packed ? packed->to_msb_string() : std::string { };
}

[[nodiscard]] bool run_functional_coverage(
    fsim::app::Simulation& simulation)
{
    const auto result = simulation.run();
    const auto successful_run = result.status == fsim::runtime::RunStatus::completed
        || result.status == fsim::runtime::RunStatus::stopped;
    if (!successful_run
        || (result.simulator_status && *result.simulator_status != 0)) {
        std::cerr << "functional coverage simulation oracle failed\n";
        return false;
    }
    return true;
}

[[nodiscard]] std::optional<std::string>
validate_functional_coverage_roundtrip(
    const Options& options)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::load_design_artifact(options.design, diagnostics);
    if (!project || diagnostics.has_error()) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
        return std::nullopt;
    }
    const auto path
        = (options.file_root / options.coverage_path).lexically_normal();
    std::ifstream input { path, std::ios::binary };
    if (!input) {
        std::cerr << "functional coverage database is missing: " << path << '\n';
        return std::nullopt;
    }
    const std::string bytes {
        std::istreambuf_iterator<char> { input },
        std::istreambuf_iterator<char> { }
    };
    if (bytes.empty()) {
        std::cerr << "functional coverage database is empty: " << path << '\n';
        return std::nullopt;
    }
    const auto decoded = fsim::app::deserialize_systemverilog_coverage_state(
        bytes, path.string(), project->systemverilog_hir, project->semantics,
        diagnostics);
    if (!decoded || decoded->reports.empty() || decoded->instances.empty()
        || !std::ranges::all_of(decoded->reports, [](const auto& report) {
               return report.coverage.goal_reached;
           })) {
        if (diagnostics.has_error()) {
            fsim::diagnostic::print_text(std::cerr, diagnostics);
        }
        std::cerr << "functional coverage roundtrip oracle failed\n";
        return std::nullopt;
    }
    auto canonical = *decoded;
    for (auto& event : canonical.callback_events) {
        // The execution backend is provenance, not coverage semantics. Keep
        // every callback field but normalize this one backend-specific enum
        // before using the existing validated codec for canonical hashing.
        event.mode = fsim::frontend::SystemVerilogCoverageExecutionMode::Interpreter;
    }
    const auto canonical_bytes = fsim::app::serialize_systemverilog_coverage_state(
        canonical, project->systemverilog_hir, project->semantics, diagnostics);
    if (!canonical_bytes || diagnostics.has_error()) {
        if (diagnostics.has_error()) {
            fsim::diagnostic::print_text(std::cerr, diagnostics);
        }
        std::cerr << "functional coverage canonicalization failed\n";
        return std::nullopt;
    }
    const auto semantic_sha256 = fsim::support::Sha256::hex(
        fsim::support::Sha256::digest(*canonical_bytes));
    std::cout << "PASS: simplification functional coverage roundtrip reports="
              << decoded->reports.size() << " instances="
              << decoded->instances.size() << '\n';
    std::cout << "PASS: simplification functional coverage semantic_sha256="
              << semantic_sha256 << '\n';
    return semantic_sha256;
}

[[nodiscard]] std::string signal_name(
    const std::string_view root, const std::string_view kind,
    const std::size_t index)
{
    return std::string { root } + "." + std::string { kind } + "_"
        + std::to_string(index);
}

[[nodiscard]] bool final_values_are_correct(
    fsim::app::Simulation& simulation, const std::string_view root)
{
    for (std::size_t index = 0; index < 16U; ++index) {
        const auto counter
            = simulation.find_signal(signal_name(root, "counter_q", index));
        const auto inverted
            = simulation.find_signal(signal_name(root, "child_y", index));
        if (!counter || !inverted
            || simulation.read_signal(*counter).to_msb_string() != "10100000"
            || simulation.read_signal(*inverted).to_msb_string() != "01011111") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool observe(
    fsim::app::Simulation& simulation, const std::string_view root)
{
    std::map<fsim::runtime::simir::SignalId, std::size_t> selected;
    for (std::size_t index = 0; index < 16U; ++index) {
        for (const auto kind : { "counter_q", "child_y" }) {
            const auto signal
                = simulation.find_signal(signal_name(root, kind, index));
            if (!signal) {
                std::cerr << "cannot resolve observer signal "
                          << signal_name(root, kind, index) << '\n';
                return false;
            }
            selected.emplace(*signal, selected.size());
        }
    }
    std::uint64_t events { };
    Checksum checksum;
    const auto token = simulation.add_signal_change_hook(
        [&](const auto signal, const auto& value, const auto time,
            const auto delta) {
            const auto found = selected.find(signal);
            if (found == selected.end()) {
                return;
            }
            ++events;
            checksum.add(found->second);
            checksum.add(time);
            checksum.add(delta);
            checksum.add(value.to_msb_string());
        });
    const auto result = simulation.run();
    simulation.remove_signal_change_hook(token);
    const auto successful_run = result.status == fsim::runtime::RunStatus::completed
        || result.status == fsim::runtime::RunStatus::stopped;
    if (!successful_run
        || (result.simulator_status && *result.simulator_status != 0)
        || events < 3'200'000U
        || !final_values_are_correct(simulation, root)) {
        std::cerr << "observer oracle failed: events=" << events << '\n';
        return false;
    }
    std::cout << "PASS: simplification observer events=" << events
              << " checksum=" << std::hex << checksum.value() << std::dec
              << '\n';
    return true;
}

[[nodiscard]] bool check_history_point(
    fsim::runtime::SystemVerilogVpiDataReadService& reader,
    const fsim::runtime::SystemVerilogVpiDataReadHandle traverse,
    const std::uint64_t requested_time, const std::uint64_t requested_time_ns,
    const std::string_view expected, Checksum& checksum)
{
    using Control = fsim::runtime::SystemVerilogVpiDataReadControl;
    const auto position = fsim::runtime::SystemVerilogVpiDataReadPosition {
        requested_time, std::numeric_limits<std::uint64_t>::max()
    };
    const auto selected = reader.go_to(traverse, Control::Time, position);
    if (!selected) {
        std::cerr << "history query failed at " << requested_time_ns
                  << "ns requested_time=" << requested_time
                  << " query_error="
                  << static_cast<unsigned>(selected.error) << '\n';
        return false;
    }
    const auto sample_time = reader.time(selected.value);
    const auto value = reader.value(selected.value);
    const auto actual_value = value.value
        ? packed_bits(*value.value) : std::string { "<none>" };
    if (!sample_time || !value || sample_time.value.time != requested_time
        || actual_value != expected) {
        std::cerr << "history sample mismatch at " << requested_time_ns
                  << "ns requested_time=" << requested_time
                  << " expected_time=" << requested_time
                  << " actual_time=" << sample_time.value.time
                  << " expected_value=" << expected
                  << " actual_value=" << actual_value
                  << " time_error="
                  << static_cast<unsigned>(sample_time.error)
                  << " value_error="
                  << static_cast<unsigned>(value.error) << '\n';
        return false;
    }
    checksum.add(sample_time.value.time);
    checksum.add(sample_time.value.delta);
    checksum.add(expected);
    return true;
}

[[nodiscard]] std::optional<std::uint64_t> ticks_per_nanosecond(
    const std::string_view resolution)
{
    const std::array units {
        std::pair { std::string_view { "fs" }, UINT64_C(1) },
        std::pair { std::string_view { "ps" }, UINT64_C(1000) },
        std::pair { std::string_view { "ns" }, UINT64_C(1000000) },
        std::pair { std::string_view { "us" }, UINT64_C(1000000000) },
    };
    for (const auto& [suffix, femtoseconds] : units) {
        if (!resolution.ends_with(suffix)) {
            continue;
        }
        const auto magnitude = parse_unsigned(
            resolution.substr(0, resolution.size() - suffix.size()));
        if (!magnitude || *magnitude == 0U) {
            return std::nullopt;
        }
        if (*magnitude
            > std::numeric_limits<std::uint64_t>::max() / femtoseconds) {
            return std::nullopt;
        }
        const auto resolution_femtoseconds = *magnitude * femtoseconds;
        if (resolution_femtoseconds > 1'000'000U
            || 1'000'000U % resolution_femtoseconds != 0U) {
            return std::nullopt;
        }
        return 1'000'000U / resolution_femtoseconds;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::uint64_t> scale_nanoseconds(
    const std::uint64_t nanoseconds, const std::uint64_t tick_scale)
{
    if (tick_scale == 0U
        || nanoseconds > std::numeric_limits<std::uint64_t>::max() / tick_scale) {
        return std::nullopt;
    }
    return nanoseconds * tick_scale;
}

[[nodiscard]] std::string inverted_bits(const std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const auto bit : value) {
        result.push_back(bit == '0' ? '1' : bit == '1' ? '0' : bit);
    }
    return result;
}

[[nodiscard]] bool history(
    fsim::app::Simulation& simulation, const std::string_view root)
{
    auto& objects = simulation.systemverilog_vpi_objects();
    auto& reader = simulation.systemverilog_vpi_data_read();
    const auto counter = objects.find(signal_name(root, "counter_q", 0U));
    const auto inverted = objects.find(signal_name(root, "child_y", 0U));
    if (!counter || !inverted
        || reader.load(counter.value->handle)
            != fsim::runtime::SystemVerilogVpiDataReadError::None
        || reader.load(inverted.value->handle)
            != fsim::runtime::SystemVerilogVpiDataReadError::None) {
        std::cerr << "cannot load selected history objects\n";
        return false;
    }
    const auto result = simulation.run();
    const auto counter_traverse = reader.create_traverse(counter.value->handle);
    const auto inverted_traverse = reader.create_traverse(inverted.value->handle);
    const auto successful_run = result.status == fsim::runtime::RunStatus::completed
        || result.status == fsim::runtime::RunStatus::stopped;
    if (!successful_run
        || (result.simulator_status && *result.simulator_status != 0)
        || !counter_traverse || !inverted_traverse
        || !final_values_are_correct(simulation, root)) {
        std::cerr << "history simulation oracle failed\n";
        return false;
    }
    const auto tick_scale = ticks_per_nanosecond(simulation.time_resolution());
    if (!tick_scale) {
        std::cerr << "unsupported simulation time resolution: "
                  << simulation.time_resolution() << '\n';
        return false;
    }
    Checksum checksum;
    const std::array points {
        std::pair { UINT64_C(6), std::string_view { "00000001" } },
        std::pair { UINT64_C(514), std::string_view { "11111111" } },
        std::pair { UINT64_C(516), std::string_view { "00000000" } },
        std::pair { UINT64_C(200004), std::string_view { "10100000" } },
    };
    for (const auto& [time_ns, value] : points) {
        const auto requested_time = scale_nanoseconds(time_ns, *tick_scale);
        if (!requested_time) {
            std::cerr << "history checkpoint time scaling overflow\n";
            return false;
        }
        if (!check_history_point(
                reader, counter_traverse.value, *requested_time, time_ns,
                value, checksum)) {
            std::cerr << "counter history mismatch at time " << time_ns
                      << "ns\n";
            return false;
        }
        const auto inverted_value = inverted_bits(value);
        if (!check_history_point(
                reader, inverted_traverse.value, *requested_time, time_ns,
                inverted_value, checksum)) {
            std::cerr << "inverted history mismatch at time " << time_ns
                      << "ns\n";
            return false;
        }
    }
    using Control = fsim::runtime::SystemVerilogVpiDataReadControl;
    const auto maximum = reader.go_to(counter_traverse.value, Control::MaximumTime);
    const auto previous = maximum
        ? reader.go_to(maximum.value, Control::PreviousValueChange)
        : fsim::runtime::SystemVerilogVpiDataReadHandleResult { };
    const auto previous_time = previous ? reader.time(previous.value)
                                        : fsim::runtime::SystemVerilogVpiDataReadTimeResult { };
    const auto previous_value = previous ? reader.value(previous.value)
                                          : fsim::runtime::SystemVerilogVpiDataReadValueResult { };
    const auto previous_expected_time = scale_nanoseconds(200001U, *tick_scale);
    if (!previous || !previous_time || !previous_value
        || !previous_expected_time
        || previous_time.value.time != *previous_expected_time
        || packed_bits(*previous_value.value) != "10011111") {
        std::cerr << "previous-value-change history oracle failed\n";
        return false;
    }
    checksum.add(previous_time.value.time);
    checksum.add(previous_time.value.delta);
    checksum.add("10011111");
    std::cout << "PASS: simplification history checks=9 checksum=" << std::hex
              << checksum.value() << std::dec << '\n';
    return true;
}

} // namespace

int main(const int argc, char* argv[])
{
    if (argc == 2 && std::string_view { argv[1] } == "--version") {
        std::cout << "fsim simplification benchmark driver 1\n";
        return 0;
    }
    const auto options = parse_options(argc, argv);
    if (!options) {
        return 2;
    }
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::load_design_artifact(options->design, diagnostics);
    if (!project || diagnostics.has_error()) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
        return 1;
    }
    project->optimization = options->optimization;
    project->cache_path = options->cache;
    project->seed = options->seed;
    if (options->mode == "coverage") {
        project->file_root = options->file_root;
    }
    const auto setup_begin = std::chrono::steady_clock::now();
    fsim::app::Simulation simulation {
        std::move(*project), options->max_deltas, options->engine,
        fsim::app::SystemVerilogVpiRuntimeUpdates::enabled
    };
    const auto setup_elapsed = std::chrono::steady_clock::now() - setup_begin;
    std::string output;
    simulation.set_output_hook(
        [&](const auto, const std::string_view text, const bool newline,
            const auto, const auto) {
            output.append(text);
            if (newline) {
                output.push_back('\n');
            }
            std::cout << text;
            if (newline) {
                std::cout << '\n';
            }
        });
    bool report_error { };
    simulation.set_report_hook(
        [&report_error](const auto, const std::string_view message,
            const auto severity,
            const auto& source, const auto, const auto) {
            using Severity = fsim::runtime::simir::AssertionSeverity;
            report_error = report_error || severity == Severity::error
                || severity == Severity::failure;
            std::cout << source.path << ':' << source.line << ':'
                      << source.column << ": report["
                      << static_cast<unsigned>(severity) << "]: "
                      << message << '\n';
        });
    const auto run_begin = std::chrono::steady_clock::now();
    const bool passed = options->mode == "observer"
        ? observe(simulation, options->root)
        : options->mode == "history"
        ? history(simulation, options->root)
        : run_functional_coverage(simulation);
    const auto run_elapsed = std::chrono::steady_clock::now() - run_begin;
    auto native_await_elapsed = std::chrono::steady_clock::duration::zero();
    const bool profile_phases = std::getenv("FSIM_PROFILE_PHASES") != nullptr;
    if (profile_phases) {
        const auto native_await_begin = std::chrono::steady_clock::now();
        simulation.await_all_native_compilation();
        native_await_elapsed
            = std::chrono::steady_clock::now() - native_await_begin;
    }
    const auto pass_marker = options->mode == "coverage"
        ? coverage_tb_pass_marker : tb_pass_marker;
    if (output.find(pass_marker) == std::string::npos) {
        std::cerr << "testbench PASS marker was not observed\n";
        return 1;
    }
    const auto semantic_sha256 = options->mode == "coverage"
        ? validate_functional_coverage_roundtrip(*options)
        : std::optional<std::string> { };
    const bool roundtrip = options->mode != "coverage"
        || semantic_sha256.has_value();
    if (profile_phases) {
        const auto milliseconds = [](const auto duration) {
            return std::chrono::duration<double, std::milli>(duration).count();
        };
        std::cout << "FSIM-PROFILE setup_ms=" << milliseconds(setup_elapsed)
                  << " run_ms=" << milliseconds(run_elapsed)
                  << " native_await_ms=" << milliseconds(native_await_elapsed)
                  << '\n';
    }
    return passed && roundtrip && !report_error ? 0 : 1;
}
