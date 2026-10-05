// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"
#include "fsim/app/application.hpp"
#include "fsim/support/environment.hpp"
#include "../../src/app/application_runtime_path_codec.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fsim::tests::elaboration {
namespace {

int set_profile_environment(const char* const value)
{
#if defined(_WIN32)
    return ::_putenv_s("FSIM_PROFILE_PHASES", value ? value : "");
#else
    return value ? ::setenv("FSIM_PROFILE_PHASES", value, 1)
                 : ::unsetenv("FSIM_PROFILE_PHASES");
#endif
}

class ProfileCapture final {
public:
    explicit ProfileCapture(std::ostringstream& output)
        : previous_environment_ {
              fsim::support::environment_variable("FSIM_PROFILE_PHASES")
          }
    {
        if (set_profile_environment("1") != 0) {
            throw std::runtime_error {
                "failed to enable callable template profiling"
            };
        }
        previous_stream_ = std::cerr.rdbuf(output.rdbuf());
    }

    ProfileCapture(const ProfileCapture&) = delete;
    ProfileCapture& operator=(const ProfileCapture&) = delete;

    ~ProfileCapture()
    {
        std::cerr.rdbuf(previous_stream_);
        const auto restored = set_profile_environment(
            previous_environment_ ? previous_environment_->c_str() : nullptr);
        (void)restored;
        assert(restored == 0);
    }

private:
    std::optional<std::string> previous_environment_;
    std::streambuf* previous_stream_ { };
};

std::uint64_t profile_metric(
    const std::string_view row, const std::string_view name)
{
    const auto marker = std::string { name } + "=";
    const auto position = row.find(marker);
    assert(position != std::string_view::npos);
    return std::stoull(std::string { row.substr(
        position + marker.size()) });
}

const fsim::runtime::simir::Process* find_writer(
    const fsim::elaboration::ElaboratedDesign& design,
    const fsim::runtime::simir::SignalId signal)
{
    for (const auto& process : design.processes()) {
        if (std::ranges::any_of(
                process.driver_regions,
                [&](const auto& region) { return region.signal == signal; })) {
            return &process;
        }
    }
    return nullptr;
}

const fsim::runtime::simir::Process* find_writer_at(
    const fsim::elaboration::ElaboratedDesign& design,
    const fsim::runtime::simir::SignalId signal,
    const std::uint32_t offset)
{
    for (const auto& process : design.processes()) {
        if (std::ranges::any_of(process.driver_regions,
                [&](const auto& region) {
                    return region.signal == signal
                        && region.offset == offset && region.width == 4U;
                })) {
            return &process;
        }
    }
    return nullptr;
}

bool reads_signal(
    const fsim::runtime::simir::Process& process,
    const fsim::runtime::simir::SignalId signal)
{
    using namespace fsim::runtime::simir;
    for (std::size_t index = 0U; index < process.operations.size(); ++index) {
        const auto operation = process.operations.expanded(index);
        if (const auto* read = operation_get_if<ReadSignal>(&operation);
            read != nullptr && read->signal == signal) {
            return true;
        }
    }
    return false;
}

std::optional<std::uint32_t> callable_frame_identity(
    const fsim::runtime::simir::Process& process)
{
    std::optional<std::uint32_t> result;
    std::size_t pushes { };
    std::size_t pops { };
    std::size_t calls { };
    for (std::size_t index = 0U; index < process.operations.size(); ++index) {
        const auto operation = process.operations.expanded(index);
        if (const auto* push =
                fsim::runtime::simir::operation_get_if<
                    fsim::runtime::simir::CallableFramePush>(&operation)) {
            ++pushes;
            if (result) {
                return std::nullopt;
            }
            result = push->identity;
        } else if (const auto* pop =
                fsim::runtime::simir::operation_get_if<
                    fsim::runtime::simir::CallableFramePop>(&operation)) {
            ++pops;
            if (result && *result != pop->identity) {
                return std::nullopt;
            }
        } else if (fsim::runtime::simir::operation_get_if<
                       fsim::runtime::simir::Call>(&operation)) {
            ++calls;
        }
    }
    if (pushes != 1U || pops != 1U || calls != 1U) {
        return std::nullopt;
    }
    return result;
}

bool same_debug_layout(
    const fsim::runtime::simir::Process& left,
    const fsim::runtime::simir::Process& right)
{
    if (left.debug_locals.size() != right.debug_locals.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.debug_locals.size(); ++index) {
        const auto& lhs = left.debug_locals[index];
        const auto& rhs = right.debug_locals[index];
        if (lhs.name != rhs.name || lhs.type_name != rhs.type_name
            || lhs.register_id != rhs.register_id || lhs.width != rhs.width
            || lhs.source != rhs.source
            || lhs.integer_lower != rhs.integer_lower
            || lhs.integer_upper != rhs.integer_upper
            || lhs.value_kind != rhs.value_kind
            || lhs.enumeration_literals != rhs.enumeration_literals
            || lhs.systemverilog_scalar != rhs.systemverilog_scalar) {
            return false;
        }
    }
    return true;
}

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

struct ApplicationCapture {
    fsim::runtime::RunResult result;
    std::array<std::string, 2U> values;
    std::size_t compiled_processes { };
};

ApplicationCapture run_callable_application(
    const std::filesystem::path& directory,
    const fsim::project::Optimization optimization,
    const fsim::app::SimulationEngine engine)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "sv-callable-template-application";
    config.project.top = "sv:work.callable_application_top";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "native-cache-o0"
                : "native-cache-o2");
    config.run.max_deltas = 1000U;

    fsim::project::SourceSet source_set;
    source_set.language = fsim::project::Language::system_verilog;
    source_set.standard = "2017";
    source_set.library = "work";
    source_set.files.push_back(directory / "callable-application.sv");
    config.source_sets.push_back(std::move(source_set));

    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(project);

    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    ApplicationCapture capture;
    if (engine == fsim::app::SimulationEngine::compiled) {
        simulation.await_all_native_compilation();
    }
    capture.compiled_processes = simulation.compiled_process_count();
    const auto left_input = simulation.find_signal(
        "callable_application_top.left_value");
    const auto right_input = simulation.find_signal(
        "callable_application_top.right_value");
    const auto left = simulation.find_signal(
        "callable_application_top.left_result");
    const auto right = simulation.find_signal(
        "callable_application_top.right_result");
    assert(left_input && right_input && left && right);
    simulation.deposit_signal(
        *left_input, fsim::runtime::PackedLogic4::from_msb_string("0001"));
    simulation.deposit_signal(
        *right_input, fsim::runtime::PackedLogic4::from_msb_string("0010"));
    capture.result = simulation.run();
    capture.values = {
        simulation.read_signal(*left).to_msb_string(),
        simulation.read_signal(*right).to_msb_string()
    };
    return capture;
}

void test_callable_application_differential(
    const fsim::project::Optimization optimization)
{
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-callable-template-application-" + std::to_string(nonce))
    };
    std::filesystem::create_directories(directory.path);
    const auto source_path = directory.path / "callable-application.sv";
    {
        std::ofstream source { source_path };
        source << R"(
module callable_application_leaf #(
  parameter logic [3:0] MASK = 4'h5
) (
  input logic [3:0] value,
  output logic [3:0] result
);
  function automatic logic [3:0] transform(input logic [3:0] argument);
    logic [3:0] temporary;
    temporary = argument ^ MASK;
    return temporary;
  endfunction

  always_comb result = transform(value);
endmodule

module callable_application_top;
  logic [3:0] left_value;
  logic [3:0] right_value;
  logic [3:0] left_result;
  logic [3:0] right_result;

  callable_application_leaf #(.MASK(4'h5)) left (
    .value(left_value), .result(left_result));
  callable_application_leaf #(.MASK(4'h5)) right (
    .value(right_value), .result(right_result));
endmodule
)";
        assert(source.good());
    }

    const auto reference = run_callable_application(
        directory.path, optimization,
        fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_callable_application(
        directory.path, optimization,
        fsim::app::SimulationEngine::compiled);
    assert(reference.result.status == fsim::runtime::RunStatus::completed);
    assert(compiled.result.status == reference.result.status);
    assert(compiled.result.time == reference.result.time);
    assert(compiled.result.delta == reference.result.delta);
    assert((reference.values == std::array<std::string, 2U> {
        "0100", "0111" }));
    assert(compiled.values == reference.values);
    assert(reference.compiled_processes == 0U);
#if defined(FSIM_HAS_LLVM)
    // This fixture contains only the two call-bearing leaf processes.
    // Requiring both to be native proves each remapped callable process used
    // the LLVM application route.
    assert(compiled.compiled_processes == 2U);
#else
    assert(compiled.compiled_processes == 0U);
#endif
}

} // namespace

void test_systemverilog_callable_process_template_replay()
{
    using fsim::runtime::simir::Process;
    using fsim::runtime::simir::SignalId;

    const auto parsed = fsim::frontend::parse_text(
        "sv-callable-process-template-replay.sv",
        R"(
module callable_template_leaf #(
  parameter logic [3:0] MASK = 4'h5
) (
  input logic [3:0] value,
  output logic [3:0] primary,
  output logic [3:0] secondary
);
  function automatic logic [3:0] transform(input logic [3:0] argument);
    logic [3:0] temporary;
    temporary = argument ^ MASK;
    return temporary;
  endfunction

  always_comb primary = transform(value);
  always_comb secondary = transform(value ^ 4'h3);
endmodule

module callable_generated_leaf (
  input logic [3:0] value,
  output logic [7:0] primary,
  output logic [7:0] secondary
);
  function automatic logic [3:0] transform(input logic [3:0] argument);
    logic [3:0] temporary;
    temporary = argument ^ 4'h5;
    return temporary;
  endfunction

  for (genvar lane = 0; lane < 2; ++lane) begin : generated_lanes
    logic [3:0] primary_lane;
    logic [3:0] secondary_lane;
    always_comb primary_lane = transform(value ^ lane);
    always_comb secondary_lane
      = transform(value ^ lane ^ 4'h3);
    assign primary[lane * 4 +: 4] = primary_lane;
    assign secondary[lane * 4 +: 4] = secondary_lane;
  end
endmodule

module callable_template_top;
  logic [3:0] left_value;
  logic [3:0] right_value;
  logic [3:0] other_value;
  logic [3:0] left_primary;
  logic [3:0] right_primary;
  logic [3:0] other_primary;
  logic [3:0] left_secondary;
  logic [3:0] right_secondary;
  logic [3:0] other_secondary;
  logic [3:0] generated_value;
  logic [7:0] generated_primary;
  logic [7:0] generated_secondary;

  callable_template_leaf #(.MASK(4'h5)) left (
    .value(left_value), .primary(left_primary),
    .secondary(left_secondary));
  callable_template_leaf #(.MASK(4'h5)) right (
    .value(right_value), .primary(right_primary),
    .secondary(right_secondary));
  callable_template_leaf #(.MASK(4'hA)) specialized_other (
    .value(other_value), .primary(other_primary),
    .secondary(other_secondary));

  callable_generated_leaf generated (
    .value(generated_value), .primary(generated_primary),
    .secondary(generated_secondary));

  initial begin
    left_value = 4'h1;
    right_value = 4'h2;
    other_value = 4'h3;
    generated_value = 4'h1;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    if (!parsed.ok()) {
        for (const auto& diagnostic : parsed.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(parsed.ok());

    std::ostringstream profile_output;
    fsim::elaboration::ElaborationResult elaborated;
    {
        const ProfileCapture profile { profile_output };
        elaborated = compile_and_elaborate(
            parsed.design, "sv:work.callable_template_top");
    }
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok() && elaborated.design);

    const auto profile = profile_output.str();
    const auto marker = std::string_view {
        "fsim-profile: systemverilog-ordinary-process-template " };
    const auto row_start = profile.find(marker);
    assert(row_start != std::string::npos);
    const auto row_end = profile.find('\n', row_start);
    const auto row = std::string_view { profile }.substr(
        row_start,
        row_end == std::string::npos
            ? profile.size() - row_start
            : row_end - row_start);
    // The top-level time-zero initializer is an ordinary process occurrence
    // that intentionally remains on the direct lowerer path.
    assert(profile_metric(row, "occurrences") == 7U);
    assert(profile_metric(row, "lowerer_calls") == 5U);
    assert(profile_metric(row, "cached_programs") == 4U);
    assert(profile_metric(row, "replays") == 2U);

    const auto generated_marker = std::string_view {
        "fsim-profile: systemverilog-generated-process-template " };
    const auto generated_start = profile.find(generated_marker);
    assert(generated_start != std::string::npos);
    const auto generated_end = profile.find('\n', generated_start);
    const auto generated_row = std::string_view { profile }.substr(
        generated_start,
        generated_end == std::string::npos
            ? profile.size() - generated_start
            : generated_end - generated_start);
    // This single generated instance establishes one reusable entry for each
    // callable process at each lane; the repeated-instance witness separately
    // proves those exact entries replay across an instance boundary.
    assert(profile_metric(generated_row, "occurrences") == 4U);
    assert(profile_metric(generated_row, "lowerer_calls") == 4U);
    assert(profile_metric(generated_row, "cached_programs") == 4U);
    assert(profile_metric(generated_row, "replays") == 0U);

    const auto concurrent_marker = std::string_view {
        "fsim-profile: systemverilog-concurrent-process-template " };
    const auto concurrent_start = profile.find(concurrent_marker);
    assert(concurrent_start != std::string::npos);
    const auto concurrent_end = profile.find('\n', concurrent_start);
    const auto concurrent_row = std::string_view { profile }.substr(
        concurrent_start,
        concurrent_end == std::string::npos
            ? profile.size() - concurrent_start
            : concurrent_end - concurrent_start);
    // Each generated lane assembles two output slices. The two assignment
    // source statements are specialized separately for each genvar value.
    assert(profile_metric(concurrent_row, "occurrences") == 4U);
    assert(profile_metric(concurrent_row, "top_occurrences") == 0U);
    assert(profile_metric(concurrent_row, "generated_occurrences") == 4U);
    assert(profile_metric(concurrent_row, "misses") == 4U);
    assert(profile_metric(concurrent_row, "hits") == 0U);
    assert(profile_metric(concurrent_row, "lowered") == 4U);
    assert(profile_metric(concurrent_row, "replayed") == 0U);
    assert(profile_metric(concurrent_row, "generated_replayed") == 0U);

    const auto signal = [&](const std::string_view name) {
        const auto id = elaborated.design->find_signal(
            "callable_template_top." + std::string { name });
        assert(id);
        return *id;
    };
    const auto left_primary_signal = signal("left_primary");
    const auto right_primary_signal = signal("right_primary");
    const auto other_primary_signal = signal("other_primary");
    const auto left_secondary_signal = signal("left_secondary");
    const auto right_secondary_signal = signal("right_secondary");
    const auto other_secondary_signal = signal("other_secondary");
    const auto left_primary = find_writer(
        *elaborated.design, left_primary_signal);
    const auto right_primary = find_writer(
        *elaborated.design, right_primary_signal);
    const auto other_primary = find_writer(
        *elaborated.design, other_primary_signal);
    const auto left_secondary = find_writer(
        *elaborated.design, left_secondary_signal);
    const auto right_secondary = find_writer(
        *elaborated.design, right_secondary_signal);
    const auto other_secondary = find_writer(
        *elaborated.design, other_secondary_signal);
    assert(left_primary && right_primary && other_primary
        && left_secondary && right_secondary && other_secondary);

    const auto generated_primary_signal = signal("generated_primary");
    const auto generated_secondary_signal = signal("generated_secondary");
    const auto primary_lane0_signal = signal(
        "generated.generated_lanes[0].primary_lane");
    const auto primary_lane1_signal = signal(
        "generated.generated_lanes[1].primary_lane");
    const auto secondary_lane0_signal = signal(
        "generated.generated_lanes[0].secondary_lane");
    const auto secondary_lane1_signal = signal(
        "generated.generated_lanes[1].secondary_lane");
    const auto* primary_assembly0 = find_writer_at(
        *elaborated.design, generated_primary_signal, 0U);
    const auto* primary_assembly1 = find_writer_at(
        *elaborated.design, generated_primary_signal, 4U);
    const auto* secondary_assembly0 = find_writer_at(
        *elaborated.design, generated_secondary_signal, 0U);
    const auto* secondary_assembly1 = find_writer_at(
        *elaborated.design, generated_secondary_signal, 4U);
    assert(primary_assembly0 && primary_assembly1
        && secondary_assembly0 && secondary_assembly1);
    assert(reads_signal(*primary_assembly0, primary_lane0_signal));
    assert(reads_signal(*primary_assembly1, primary_lane1_signal));
    assert(reads_signal(*secondary_assembly0, secondary_lane0_signal));
    assert(reads_signal(*secondary_assembly1, secondary_lane1_signal));

    assert(left_primary->operations.shares_body_with(
        right_primary->operations));
    assert(left_secondary->operations.shares_body_with(
        right_secondary->operations));
    assert(!left_primary->operations.shares_body_with(
        other_primary->operations));
    assert(!left_secondary->operations.shares_body_with(
        other_secondary->operations));

    const auto primary_identity = callable_frame_identity(*left_primary);
    const auto replayed_primary_identity
        = callable_frame_identity(*right_primary);
    const auto secondary_identity = callable_frame_identity(*left_secondary);
    const auto replayed_secondary_identity
        = callable_frame_identity(*right_secondary);
    assert(primary_identity && replayed_primary_identity
        && secondary_identity && replayed_secondary_identity);
    assert(*primary_identity == *replayed_primary_identity);
    assert(*secondary_identity == *replayed_secondary_identity);
    assert(*primary_identity != *secondary_identity);

    assert(left_primary->debug_locals.size() >= 2U);
    assert(std::ranges::any_of(
        left_primary->debug_locals,
        [](const auto& local) {
            return local.name.find("transform.argument")
                != std::string::npos;
        }));
    assert(std::ranges::any_of(
        left_primary->debug_locals,
        [](const auto& local) {
            return local.name.find("transform.temporary")
                != std::string::npos;
        }));
    assert(same_debug_layout(*left_primary, *right_primary));
    assert(same_debug_layout(*left_secondary, *right_secondary));

    fsim::diagnostic::Engine artifact_diagnostics;
    const auto encoded = fsim::app::runtime_path_codec::
        serialize_runtime_path_state(
            elaborated.design->state(), nullptr, artifact_diagnostics);
    assert(encoded);
    const auto decoded = fsim::app::runtime_path_codec::
        deserialize_runtime_path_state(
            *encoded, "sv-callable-process-template-replay", nullptr,
            artifact_diagnostics);
    assert(decoded);
    assert(decoded->processes.at(left_primary->id).operations.shares_body_with(
        decoded->processes.at(right_primary->id).operations));
    assert(decoded->processes.at(left_secondary->id).operations.shares_body_with(
        decoded->processes.at(right_secondary->id).operations));
    assert(!decoded->processes.at(left_primary->id)
                .operations.shares_body_with(
                    decoded->processes.at(other_primary->id).operations));
    assert(same_debug_layout(
        decoded->processes.at(left_primary->id),
        decoded->processes.at(right_primary->id)));
    const auto encoded_again = fsim::app::runtime_path_codec::
        serialize_runtime_path_state(
            *decoded, nullptr, artifact_diagnostics);
    assert(encoded_again && *encoded_again == *encoded);

    auto interpreter = elaborated.design->create_interpreter();
    assert(interpreter->run().status == fsim::runtime::RunStatus::completed);
    assert(interpreter->signal_value(left_primary_signal)
            .to_msb_string() == "0100");
    assert(interpreter->signal_value(right_primary_signal)
            .to_msb_string() == "0111");
    assert(interpreter->signal_value(other_primary_signal)
            .to_msb_string() == "1001");
    assert(interpreter->signal_value(left_secondary_signal)
            .to_msb_string() == "0111");
    assert(interpreter->signal_value(right_secondary_signal)
            .to_msb_string() == "0100");
    assert(interpreter->signal_value(other_secondary_signal)
            .to_msb_string() == "1010");

    assert(interpreter->signal_value(signal("generated_primary"))
            .to_msb_string() == "01010100");
    assert(interpreter->signal_value(signal("generated_secondary"))
            .to_msb_string() == "01100111");

    test_callable_application_differential(
        fsim::project::Optimization::o0);
    test_callable_application_differential(
        fsim::project::Optimization::o2);
}

} // namespace fsim::tests::elaboration
