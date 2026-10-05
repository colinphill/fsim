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
#include <utility>

namespace fsim::tests::elaboration {
namespace {

constexpr std::string_view generated_callable_source = R"(
module generated_callable_leaf #(
  parameter logic [3:0] MASK = 4'h5
) (
  input logic [3:0] value,
  output logic [7:0] primary,
  output logic [7:0] secondary
);
  function automatic logic [3:0] transform(input logic [3:0] argument);
    logic [3:0] temporary;
    temporary = argument ^ MASK;
    return temporary;
  endfunction

  generate
    if (MASK == 4'h5) begin : mask_five
      for (genvar lane = 0; lane < 2; ++lane) begin : generated_lanes
        logic [3:0] primary_lane;
        logic [3:0] secondary_lane;
        always_comb primary_lane = transform(value ^ lane);
        always_comb secondary_lane
          = transform(value ^ lane ^ 4'h3);
        assign primary[lane * 4 +: 4] = primary_lane;
        assign secondary[lane * 4 +: 4] = secondary_lane;
      end
    end else begin : mask_other
      for (genvar lane = 0; lane < 2; ++lane) begin : generated_lanes
        logic [3:0] primary_lane;
        logic [3:0] secondary_lane;
        always_comb primary_lane = transform(value ^ lane);
        always_comb secondary_lane
          = transform(value ^ lane ^ 4'h3);
        assign primary[lane * 4 +: 4] = primary_lane;
        assign secondary[lane * 4 +: 4] = secondary_lane;
      end
    end
  endgenerate
endmodule

module generated_callable_top;
  logic [3:0] left_value;
  logic [3:0] right_value;
  logic [3:0] specialized_value;
  logic [7:0] left_primary;
  logic [7:0] left_secondary;
  logic [7:0] right_primary;
  logic [7:0] right_secondary;
  logic [7:0] specialized_primary;
  logic [7:0] specialized_secondary;

  generated_callable_leaf #(.MASK(4'h5)) left (
    .value(left_value), .primary(left_primary),
    .secondary(left_secondary));
  generated_callable_leaf #(.MASK(4'h5)) right (
    .value(right_value), .primary(right_primary),
    .secondary(right_secondary));
  generated_callable_leaf #(.MASK(4'hA)) specialized (
    .value(specialized_value), .primary(specialized_primary),
    .secondary(specialized_secondary));
endmodule
)";

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
                "failed to enable generated callable template profiling"
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

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

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

const fsim::runtime::simir::Process* find_writer(
    const fsim::elaboration::ElaboratedDesign& design,
    const fsim::runtime::simir::SignalId signal)
{
    for (const auto& process : design.processes()) {
        if (std::ranges::any_of(process.driver_regions,
                [&](const auto& region) {
                    return region.signal == signal && region.offset == 0U
                        && region.width == 0U && region.whole;
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
    using namespace fsim::runtime::simir;
    std::optional<std::uint32_t> result;
    std::size_t pushes { };
    std::size_t pops { };
    std::size_t calls { };
    for (std::size_t index = 0U; index < process.operations.size(); ++index) {
        const auto operation = process.operations.expanded(index);
        if (const auto* push = operation_get_if<CallableFramePush>(&operation)) {
            ++pushes;
            if (result) {
                return std::nullopt;
            }
            result = push->identity;
        } else if (const auto* pop
            = operation_get_if<CallableFramePop>(&operation)) {
            ++pops;
            if (result && *result != pop->identity) {
                return std::nullopt;
            }
        } else if (operation_get_if<Call>(&operation) != nullptr) {
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

struct ApplicationCapture {
    fsim::runtime::RunResult result;
    std::array<std::string, 6U> values;
    std::size_t compiled_processes { };
};

ApplicationCapture run_application(
    const std::filesystem::path& directory,
    const fsim::project::Optimization optimization,
    const fsim::app::SimulationEngine engine)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "sv-generated-callable-template";
    config.project.top = "sv:work.generated_callable_top";
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
    source_set.files.push_back(directory / "generated-callable.sv");
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

    const auto find = [&](const std::string_view name) {
        const auto signal = simulation.find_signal(
            "generated_callable_top." + std::string { name });
        assert(signal);
        return *signal;
    };
    simulation.deposit_signal(
        find("left_value"), fsim::runtime::PackedLogic4::from_msb_string("0001"));
    simulation.deposit_signal(
        find("right_value"), fsim::runtime::PackedLogic4::from_msb_string("0010"));
    simulation.deposit_signal(
        find("specialized_value"),
        fsim::runtime::PackedLogic4::from_msb_string("0011"));
    capture.result = simulation.run();
    capture.values = {
        simulation.read_signal(find("left_primary")).to_msb_string(),
        simulation.read_signal(find("left_secondary")).to_msb_string(),
        simulation.read_signal(find("right_primary")).to_msb_string(),
        simulation.read_signal(find("right_secondary")).to_msb_string(),
        simulation.read_signal(find("specialized_primary")).to_msb_string(),
        simulation.read_signal(find("specialized_secondary")).to_msb_string(),
    };
    return capture;
}

void test_application_differential(
    const fsim::project::Optimization optimization)
{
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-generated-callable-" + std::to_string(nonce))
    };
    std::filesystem::create_directories(directory.path);
    {
        std::ofstream source { directory.path / "generated-callable.sv" };
        source << generated_callable_source;
        assert(source.good());
    }

    const auto reference = run_application(
        directory.path, optimization,
        fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_application(
        directory.path, optimization,
        fsim::app::SimulationEngine::compiled);
    assert(reference.result.status == fsim::runtime::RunStatus::completed);
    assert(compiled.result.status == reference.result.status);
    assert(compiled.result.time == reference.result.time);
    assert(compiled.result.delta == reference.result.delta);
    assert((reference.values == std::array<std::string, 6U> {
        "01010100", "01100111", "01100111", "01010100",
        "10001001", "10111010" }));
    assert(compiled.values == reference.values);
    assert(reference.compiled_processes == 0U);
#if defined(FSIM_HAS_LLVM)
    // Twelve generated callable lane processes and twelve generated
    // continuous slice-assembly processes are compiled across the instances.
    assert(compiled.compiled_processes == 24U);
#else
    assert(compiled.compiled_processes == 0U);
#endif
}

} // namespace

void test_systemverilog_generated_callable_process_template_replay()
{
    const auto parsed = fsim::frontend::parse_text(
        "sv-generated-callable-process-template-replay.sv",
        generated_callable_source,
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
            parsed.design, "sv:work.generated_callable_top");
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
        "fsim-profile: systemverilog-generated-process-template " };
    const auto row_start = profile.find(marker);
    assert(row_start != std::string::npos);
    const auto row_end = profile.find('\n', row_start);
    const auto row = std::string_view { profile }.substr(
        row_start,
        row_end == std::string::npos
            ? profile.size() - row_start
            : row_end - row_start);
    // Two matching instances replay their exact lane bodies. The third
    // instance selects the other generate branch and a different parameter
    // identity, so it lowers four separate bodies.
    assert(profile_metric(row, "occurrences") == 12U);
    assert(profile_metric(row, "lowerer_calls") == 8U);
    assert(profile_metric(row, "cached_programs") == 8U);
    assert(profile_metric(row, "replays") == 4U);
    assert(profile_metric(row, "rejected") == 0U);

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
    // The same two assignment source statements are specialized once for
    // each genvar value. Matching module instances replay both exact bodies;
    // the other generate branch has a distinct selected-generate identity.
    assert(profile_metric(concurrent_row, "occurrences") == 12U);
    assert(profile_metric(concurrent_row, "top_occurrences") == 0U);
    assert(profile_metric(concurrent_row, "generated_occurrences") == 12U);
    assert(profile_metric(concurrent_row, "misses") == 8U);
    assert(profile_metric(concurrent_row, "hits") == 4U);
    assert(profile_metric(concurrent_row, "rejected") == 0U);
    assert(profile_metric(concurrent_row, "lowered") == 8U);
    assert(profile_metric(concurrent_row, "replayed") == 4U);
    assert(profile_metric(concurrent_row, "top_replayed") == 0U);
    assert(profile_metric(concurrent_row, "generated_replayed") == 4U);
    assert(profile_metric(concurrent_row, "lowerer_calls") == 8U);
    assert(profile_metric(concurrent_row, "generated_lowerer_calls") == 8U);

    const auto signal = [&](const std::string_view name) {
        const auto id = elaborated.design->find_signal(
            "generated_callable_top." + std::string { name });
        assert(id);
        return *id;
    };
    const auto slice_writer = [&](const std::string_view name,
                                  const std::uint32_t offset) {
        const auto* result = find_writer_at(
            *elaborated.design,
            signal(name), offset);
        assert(result != nullptr);
        return result;
    };
    const auto whole_writer = [&](const std::string_view name) {
        const auto* result = find_writer(*elaborated.design, signal(name));
        assert(result != nullptr);
        return result;
    };

    const auto* left_primary_lane0 = whole_writer(
        "left.mask_five.generated_lanes[0].primary_lane");
    const auto* left_primary_lane1 = whole_writer(
        "left.mask_five.generated_lanes[1].primary_lane");
    const auto* right_primary_lane0 = whole_writer(
        "right.mask_five.generated_lanes[0].primary_lane");
    const auto* right_primary_lane1 = whole_writer(
        "right.mask_five.generated_lanes[1].primary_lane");
    const auto* other_primary_lane0 = whole_writer(
        "specialized.mask_other.generated_lanes[0].primary_lane");
    const auto* other_primary_lane1 = whole_writer(
        "specialized.mask_other.generated_lanes[1].primary_lane");
    const auto* left_secondary_lane0 = whole_writer(
        "left.mask_five.generated_lanes[0].secondary_lane");
    const auto* left_secondary_lane1 = whole_writer(
        "left.mask_five.generated_lanes[1].secondary_lane");
    const auto* right_secondary_lane0 = whole_writer(
        "right.mask_five.generated_lanes[0].secondary_lane");
    const auto* right_secondary_lane1 = whole_writer(
        "right.mask_five.generated_lanes[1].secondary_lane");
    const auto* other_secondary_lane0 = whole_writer(
        "specialized.mask_other.generated_lanes[0].secondary_lane");
    const auto* other_secondary_lane1 = whole_writer(
        "specialized.mask_other.generated_lanes[1].secondary_lane");

    const auto* left_primary_assembly0 = slice_writer("left_primary", 0U);
    const auto* left_primary_assembly1 = slice_writer("left_primary", 4U);
    const auto* right_primary_assembly0 = slice_writer("right_primary", 0U);
    const auto* right_primary_assembly1 = slice_writer("right_primary", 4U);
    const auto* other_primary_assembly0
        = slice_writer("specialized_primary", 0U);
    const auto* other_primary_assembly1
        = slice_writer("specialized_primary", 4U);
    const auto* left_secondary_assembly0
        = slice_writer("left_secondary", 0U);
    const auto* left_secondary_assembly1
        = slice_writer("left_secondary", 4U);
    const auto* right_secondary_assembly0
        = slice_writer("right_secondary", 0U);
    const auto* right_secondary_assembly1
        = slice_writer("right_secondary", 4U);
    const auto* other_secondary_assembly0
        = slice_writer("specialized_secondary", 0U);
    const auto* other_secondary_assembly1
        = slice_writer("specialized_secondary", 4U);

    assert(reads_signal(*left_primary_assembly0, signal(
        "left.mask_five.generated_lanes[0].primary_lane")));
    assert(reads_signal(*left_primary_assembly1, signal(
        "left.mask_five.generated_lanes[1].primary_lane")));
    assert(reads_signal(*right_primary_assembly0, signal(
        "right.mask_five.generated_lanes[0].primary_lane")));
    assert(reads_signal(*right_primary_assembly1, signal(
        "right.mask_five.generated_lanes[1].primary_lane")));
    assert(reads_signal(*other_primary_assembly0, signal(
        "specialized.mask_other.generated_lanes[0].primary_lane")));
    assert(reads_signal(*other_primary_assembly1, signal(
        "specialized.mask_other.generated_lanes[1].primary_lane")));
    assert(reads_signal(*left_secondary_assembly0, signal(
        "left.mask_five.generated_lanes[0].secondary_lane")));
    assert(reads_signal(*left_secondary_assembly1, signal(
        "left.mask_five.generated_lanes[1].secondary_lane")));
    assert(reads_signal(*right_secondary_assembly0, signal(
        "right.mask_five.generated_lanes[0].secondary_lane")));
    assert(reads_signal(*right_secondary_assembly1, signal(
        "right.mask_five.generated_lanes[1].secondary_lane")));
    assert(reads_signal(*other_secondary_assembly0, signal(
        "specialized.mask_other.generated_lanes[0].secondary_lane")));
    assert(reads_signal(*other_secondary_assembly1, signal(
        "specialized.mask_other.generated_lanes[1].secondary_lane")));

    assert(left_primary_lane0->operations.shares_body_with(
        right_primary_lane0->operations));
    assert(left_primary_lane1->operations.shares_body_with(
        right_primary_lane1->operations));
    assert(left_secondary_lane0->operations.shares_body_with(
        right_secondary_lane0->operations));
    assert(left_secondary_lane1->operations.shares_body_with(
        right_secondary_lane1->operations));
    assert(left_secondary_assembly0->operations.shares_body_with(
        right_secondary_assembly0->operations));
    assert(left_secondary_assembly1->operations.shares_body_with(
        right_secondary_assembly1->operations));
    assert(left_primary_assembly0->operations.shares_body_with(
        right_primary_assembly0->operations));
    assert(left_primary_assembly1->operations.shares_body_with(
        right_primary_assembly1->operations));
    assert(left_primary_lane0->name.find("generated_lanes[0]")
        != std::string::npos);
    assert(left_primary_lane1->name.find("generated_lanes[1]")
        != std::string::npos);
    assert(!left_primary_lane0->operations.shares_body_with(
        left_primary_lane1->operations));
    assert(!left_primary_lane0->operations.shares_body_with(
        other_primary_lane0->operations));
    assert(!left_primary_lane1->operations.shares_body_with(
        other_primary_lane1->operations));
    assert(!left_secondary_lane0->operations.shares_body_with(
        other_secondary_lane0->operations));
    assert(!left_secondary_lane1->operations.shares_body_with(
        other_secondary_lane1->operations));

    for (const auto* process : { left_primary_lane0, left_primary_lane1,
             right_primary_lane0, right_primary_lane1 }) {
        assert(callable_frame_identity(*process));
        assert(process->debug_locals.size() >= 2U);
        assert(std::ranges::any_of(process->debug_locals,
            [](const auto& local) {
                return local.name.find("transform.argument")
                    != std::string::npos;
            }));
        assert(std::ranges::any_of(process->debug_locals,
            [](const auto& local) {
                return local.name.find("transform.temporary")
                    != std::string::npos;
            }));
    }
    assert(callable_frame_identity(*left_primary_lane0)
        == callable_frame_identity(*right_primary_lane0));
    assert(callable_frame_identity(*left_primary_lane1)
        == callable_frame_identity(*right_primary_lane1));
    assert(same_debug_layout(
        *left_primary_lane0, *right_primary_lane0));
    assert(same_debug_layout(
        *left_primary_lane1, *right_primary_lane1));

    fsim::diagnostic::Engine artifact_diagnostics;
    const auto encoded = fsim::app::runtime_path_codec::
        serialize_runtime_path_state(
            elaborated.design->state(), nullptr, artifact_diagnostics);
    assert(encoded);
    const auto decoded = fsim::app::runtime_path_codec::
        deserialize_runtime_path_state(
            *encoded, "sv-generated-callable-template-replay", nullptr,
            artifact_diagnostics);
    assert(decoded);
    assert(decoded->processes.at(left_primary_lane0->id)
        .operations.shares_body_with(
            decoded->processes.at(right_primary_lane0->id).operations));
    assert(decoded->processes.at(left_primary_lane1->id)
        .operations.shares_body_with(
            decoded->processes.at(right_primary_lane1->id).operations));
    assert(!decoded->processes.at(left_primary_lane0->id)
        .operations.shares_body_with(
            decoded->processes.at(left_primary_lane1->id).operations));
    assert(!decoded->processes.at(left_primary_lane0->id)
        .operations.shares_body_with(
            decoded->processes.at(other_primary_lane0->id).operations));
    assert(same_debug_layout(
        decoded->processes.at(left_primary_lane0->id),
        decoded->processes.at(right_primary_lane0->id)));
    assert(decoded->processes.at(left_primary_assembly0->id)
        .operations.shares_body_with(
            decoded->processes.at(right_primary_assembly0->id).operations));
    assert(decoded->processes.at(left_primary_assembly1->id)
        .operations.shares_body_with(
            decoded->processes.at(right_primary_assembly1->id).operations));
    assert(decoded->processes.at(left_secondary_assembly0->id)
        .operations.shares_body_with(
            decoded->processes.at(right_secondary_assembly0->id).operations));
    assert(decoded->processes.at(left_secondary_assembly1->id)
        .operations.shares_body_with(
            decoded->processes.at(right_secondary_assembly1->id).operations));
    const auto encoded_again = fsim::app::runtime_path_codec::
        serialize_runtime_path_state(
            *decoded, nullptr, artifact_diagnostics);
    assert(encoded_again && *encoded_again == *encoded);

    auto interpreter = elaborated.design->create_interpreter();
    const auto input = [&](const std::string_view instance) {
        const auto id = elaborated.design->find_signal(
            "generated_callable_top." + std::string { instance }
            + "_value");
        assert(id);
        return *id;
    };
    interpreter->deposit_signal(input("left"),
        fsim::runtime::PackedLogic4::from_msb_string("0001"));
    interpreter->deposit_signal(input("right"),
        fsim::runtime::PackedLogic4::from_msb_string("0010"));
    interpreter->deposit_signal(input("specialized"),
        fsim::runtime::PackedLogic4::from_msb_string("0011"));
    assert(interpreter->run().status == fsim::runtime::RunStatus::completed);
    const auto value = [&](const std::string_view instance,
                           const std::string_view name) {
        return interpreter->signal_value(signal(
            std::string { instance } + "_" + std::string { name }))
            .to_msb_string();
    };
    assert(value("left", "primary") == "01010100");
    assert(value("left", "secondary") == "01100111");
    assert(value("right", "primary") == "01100111");
    assert(value("right", "secondary") == "01010100");
    assert(value("specialized", "primary") == "10001001");
    assert(value("specialized", "secondary") == "10111010");

    test_application_differential(fsim::project::Optimization::o0);
    test_application_differential(fsim::project::Optimization::o2);
}

} // namespace fsim::tests::elaboration
