// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"
#include "fsim/support/environment.hpp"
#include "../../src/app/application_runtime_path_codec.hpp"
#include "../../src/elaboration/elaborated_design_process_access.hpp"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::elaboration {
namespace {

int set_template_profile_environment(
    const char* key, const char* value)
{
#if defined(_WIN32)
    return ::_putenv_s(key, value == nullptr ? "" : value);
#else
    return value == nullptr ? ::unsetenv(key) : ::setenv(key, value, 1);
#endif
}

class ProfileCapture final {
public:
    explicit ProfileCapture(std::ostringstream& output)
    : previous_environment_ {
          fsim::support::environment_variable("FSIM_PROFILE_PHASES")
      }
    {
        if (set_template_profile_environment(
                "FSIM_PROFILE_PHASES", "1") != 0) {
            throw std::runtime_error {
                "failed to enable phase profiling for process template test"
            };
        }
        previous_stream_ = std::cerr.rdbuf(output.rdbuf());
    }

    ProfileCapture(const ProfileCapture&) = delete;
    ProfileCapture& operator=(const ProfileCapture&) = delete;

    ~ProfileCapture()
    {
        std::cerr.rdbuf(previous_stream_);
        const auto restored = set_template_profile_environment(
            "FSIM_PROFILE_PHASES",
            previous_environment_
                ? previous_environment_->c_str()
                : nullptr);
        (void)restored;
        assert(restored == 0);
    }

private:
    std::optional<std::string> previous_environment_;
    std::streambuf* previous_stream_ { };
};

std::uint64_t profile_metric(
    const std::string_view row,
    const std::string_view name)
{
    const auto marker = std::string { name } + "=";
    const auto position = row.find(marker);
    assert(position != std::string_view::npos);
    return std::stoull(std::string { row.substr(
        position + marker.size()) });
}

void test_static_always_template_replay()
{
    using fsim::runtime::PackedLogic4;
    using fsim::runtime::simir::EdgeKind;
    using fsim::runtime::simir::Process;
    using fsim::runtime::simir::SignalId;
    using fsim::runtime::simir::SignalUpdateDomain;

    const auto parsed = fsim::frontend::parse_text(
        "sv-static-always-template-replay.sv",
        R"(
module static_always_leaf #(
  parameter logic INVERT = 1'b1
) (
  input logic event_clock,
  input logic event_reset,
  input logic [1:0] value,
  output logic [1:0] always_result
);
  always @(posedge event_clock or negedge event_reset)
    always_result <= value ^ INVERT;
endmodule

module static_always_generated_leaf #(
  parameter logic INVERT = 1'b1
) (
  input logic event_clock,
  input logic event_reset,
  input logic [1:0] value,
  output wire [1:0] always_result
);
  for (genvar index = 0; index < 2; ++index) begin : lanes
    logic lane_always_result;
    always @(posedge event_clock or negedge event_reset)
      lane_always_result <= value[index] ^ INVERT;
    assign always_result[index] = lane_always_result;
  end
endmodule

module static_always_template_top;
  logic ordinary_left_clock;
  logic ordinary_right_clock;
  logic ordinary_other_clock;
  logic ordinary_left_reset;
  logic ordinary_right_reset;
  logic ordinary_other_reset;
  logic generated_left_clock;
  logic generated_right_clock;
  logic generated_other_clock;
  logic generated_left_reset;
  logic generated_right_reset;
  logic generated_other_reset;
  logic [1:0] ordinary_left_value;
  logic [1:0] ordinary_right_value;
  logic [1:0] ordinary_other_value;
  logic [1:0] generated_left_value;
  logic [1:0] generated_right_value;
  logic [1:0] generated_other_value;
  wire [1:0] ordinary_left_always;
  wire [1:0] ordinary_right_always;
  wire [1:0] ordinary_other_always;
  wire [1:0] generated_left_always;
  wire [1:0] generated_right_always;
  wire [1:0] generated_other_always;

  static_always_leaf #(.INVERT(1'b1)) ordinary_left (
    .event_clock(ordinary_left_clock), .event_reset(ordinary_left_reset),
    .value(ordinary_left_value),
    .always_result(ordinary_left_always));
  static_always_leaf #(.INVERT(1'b1)) ordinary_right (
    .event_clock(ordinary_right_clock), .event_reset(ordinary_right_reset),
    .value(ordinary_right_value),
    .always_result(ordinary_right_always));
  static_always_leaf #(.INVERT(1'b0)) ordinary_other (
    .event_clock(ordinary_other_clock), .event_reset(ordinary_other_reset),
    .value(ordinary_other_value),
    .always_result(ordinary_other_always));

  static_always_generated_leaf #(.INVERT(1'b1)) generated_left (
    .event_clock(generated_left_clock), .event_reset(generated_left_reset),
    .value(generated_left_value),
    .always_result(generated_left_always));
  static_always_generated_leaf #(.INVERT(1'b1)) generated_right (
    .event_clock(generated_right_clock), .event_reset(generated_right_reset),
    .value(generated_right_value),
    .always_result(generated_right_always));
  static_always_generated_leaf #(.INVERT(1'b0)) generated_other (
    .event_clock(generated_other_clock), .event_reset(generated_other_reset),
    .value(generated_other_value),
    .always_result(generated_other_always));
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
            parsed.design, "sv:work.static_always_template_top");
    }
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok() && elaborated.design);

    const auto profile = profile_output.str();
    const auto profile_row = [&](const std::string_view name) {
        const auto marker = std::string { "fsim-profile: " }
            + std::string { name } + " ";
        const auto row_start = profile.find(marker);
        assert(row_start != std::string::npos);
        const auto row_end = profile.find('\n', row_start);
        return std::string_view { profile }.substr(
            row_start, row_end == std::string::npos
                ? profile.size() - row_start : row_end - row_start);
    };
    const auto ordinary = profile_row(
        "systemverilog-ordinary-process-template");
    assert(profile_metric(ordinary, "occurrences") == 3U);
    assert(profile_metric(ordinary, "lowerer_calls") == 2U);
    assert(profile_metric(ordinary, "cached_programs") == 2U);
    assert(profile_metric(ordinary, "replays") == 1U);
    const auto generated = profile_row(
        "systemverilog-generated-process-template");
    assert(profile_metric(generated, "occurrences") == 6U);
    assert(profile_metric(generated, "lowerer_calls")
        + profile_metric(generated, "replays") == 6U);
    assert(profile_metric(generated, "cached_programs") >= 2U);
    assert(profile_metric(generated, "replays") >= 1U);

    const auto signal = [&](const std::string_view suffix) {
        const auto id = elaborated.design->find_signal(
            "static_always_template_top." + std::string { suffix });
        assert(id);
        return *id;
    };
    const auto find_writer = [&](const SignalId destination)
        -> const Process* {
        for (const auto& process : elaborated.design->processes()) {
            for (std::size_t index = 0U;
                 index < process.operations.size(); ++index) {
                const auto operation = process.operations.expanded(index);
                const auto* update = fsim::runtime::simir::operation_get_if<
                    fsim::runtime::simir::WriteUpdate>(&operation);
                if (update != nullptr && update->signal == destination) {
                    return &process;
                }
            }
        }
        return nullptr;
    };
    const auto verify_writer = [&](const Process& process,
                                   const SignalId event_clock,
                                   const SignalId event_reset,
                                   const SignalId value_input,
                                   const SignalId destination,
                                   const std::string_view path,
                                   const bool generated_occurrence = false) {
        const auto expected_name = generated_occurrence
            ? std::string { path }
            : std::string { path } + ".process_"
                + std::to_string(process.id);
        assert(process.name == expected_name);
        assert(process.initialize);
        assert(process.static_sensitivity.size() == 2U);
        assert(std::ranges::any_of(process.static_sensitivity,
            [&](const auto& sensitivity) {
                return sensitivity.signal == event_clock
                    && sensitivity.edge == EdgeKind::posedge;
            }));
        assert(std::ranges::any_of(process.static_sensitivity,
            [&](const auto& sensitivity) {
                return sensitivity.signal == event_reset
                    && sensitivity.edge == EdgeKind::negedge;
            }));
        assert(process.driver_regions.size() == 1U);
        assert(process.driver_regions.front().signal == destination);
        bool wait_seen { };
        bool value_read { };
        bool nonblocking_write { };
        for (std::size_t index = 0U;
             index < process.operations.size(); ++index) {
            const auto operation = process.operations.expanded(index);
            wait_seen = wait_seen
                || fsim::runtime::simir::operation_holds<
                    fsim::runtime::simir::WaitSensitivity>(operation);
            if (const auto* read = fsim::runtime::simir::operation_get_if<
                    fsim::runtime::simir::ReadSignal>(&operation)) {
                value_read = value_read || read->signal == value_input;
            }
            if (const auto* update = fsim::runtime::simir::operation_get_if<
                    fsim::runtime::simir::WriteUpdate>(&operation)) {
                nonblocking_write = nonblocking_write
                    || (update->signal == destination
                        && update->domain
                            == SignalUpdateDomain::systemverilog_nba);
            }
        }
        assert(wait_seen && value_read && nonblocking_write);
    };

    const auto ordinary_left_clock = signal("ordinary_left_clock");
    const auto ordinary_right_clock = signal("ordinary_right_clock");
    const auto ordinary_other_clock = signal("ordinary_other_clock");
    const auto ordinary_left_reset = signal("ordinary_left_reset");
    const auto ordinary_right_reset = signal("ordinary_right_reset");
    const auto ordinary_other_reset = signal("ordinary_other_reset");
    const auto generated_left_clock = signal("generated_left_clock");
    const auto generated_right_clock = signal("generated_right_clock");
    const auto generated_other_clock = signal("generated_other_clock");
    const auto generated_left_reset = signal("generated_left_reset");
    const auto generated_right_reset = signal("generated_right_reset");
    const auto generated_other_reset = signal("generated_other_reset");
    assert(ordinary_left_clock != ordinary_right_clock);
    assert(ordinary_right_clock != ordinary_other_clock);
    assert(generated_left_clock != generated_right_clock);
    assert(generated_right_clock != generated_other_clock);
    assert(ordinary_left_reset != ordinary_right_reset);
    assert(ordinary_right_reset != ordinary_other_reset);
    assert(generated_left_reset != generated_right_reset);
    assert(generated_right_reset != generated_other_reset);

    const auto ordinary_left_output = signal("ordinary_left_always");
    const auto ordinary_right_output = signal("ordinary_right_always");
    const auto ordinary_other_output = signal("ordinary_other_always");
    const auto ordinary_left_formal
        = signal("ordinary_left.always_result");
    const auto ordinary_right_formal
        = signal("ordinary_right.always_result");
    const auto ordinary_other_formal
        = signal("ordinary_other.always_result");
    const auto generated_left_output = signal("generated_left_always");
    const auto generated_right_output = signal("generated_right_always");
    const auto generated_other_output = signal("generated_other_always");
    const auto generated_left_lane
        = signal("generated_left.lanes[0].lane_always_result");
    const auto generated_right_lane
        = signal("generated_right.lanes[0].lane_always_result");
    const auto generated_other_lane
        = signal("generated_other.lanes[0].lane_always_result");
    const auto generated_left_lane_one
        = signal("generated_left.lanes[1].lane_always_result");
    const auto generated_right_lane_one
        = signal("generated_right.lanes[1].lane_always_result");
    const auto generated_other_lane_one
        = signal("generated_other.lanes[1].lane_always_result");

    const auto* ordinary_left = find_writer(ordinary_left_formal);
    const auto* ordinary_right = find_writer(ordinary_right_formal);
    const auto* ordinary_other = find_writer(ordinary_other_formal);
    const auto* generated_left = find_writer(generated_left_lane);
    const auto* generated_right = find_writer(generated_right_lane);
    const auto* generated_other = find_writer(generated_other_lane);
    const auto* generated_left_one = find_writer(generated_left_lane_one);
    const auto* generated_right_one = find_writer(generated_right_lane_one);
    const auto* generated_other_one = find_writer(generated_other_lane_one);
    assert(ordinary_left && ordinary_right && ordinary_other
        && generated_left && generated_right && generated_other
        && generated_left_one && generated_right_one
        && generated_other_one);

    verify_writer(*ordinary_left, ordinary_left_clock, ordinary_left_reset,
        signal("ordinary_left_value"),
        ordinary_left_formal, "static_always_template_top.ordinary_left");
    verify_writer(*ordinary_right, ordinary_right_clock, ordinary_right_reset,
        signal("ordinary_right_value"),
        ordinary_right_formal, "static_always_template_top.ordinary_right");
    verify_writer(*ordinary_other, ordinary_other_clock, ordinary_other_reset,
        signal("ordinary_other_value"),
        ordinary_other_formal, "static_always_template_top.ordinary_other");

    const auto find_output_adapter = [&](const SignalId source,
                                         const SignalId destination) {
        return std::ranges::any_of(
            elaborated.design->processes(),
            [&](const Process& process) {
                bool source_read { };
                bool destination_write { };
                for (std::size_t index = 0U;
                     index < process.operations.size(); ++index) {
                    const auto operation
                        = process.operations.expanded(index);
                    if (const auto* read
                        = fsim::runtime::simir::operation_get_if<
                            fsim::runtime::simir::ReadSignal>(
                                &operation)) {
                        source_read = source_read
                            || read->signal == source;
                    }
                    if (const auto* update
                        = fsim::runtime::simir::operation_get_if<
                            fsim::runtime::simir::WriteUpdate>(
                                &operation)) {
                        destination_write = destination_write
                            || (update->signal == destination
                                && update->domain
                                    == SignalUpdateDomain::systemverilog_active);
                    }
                }
                return source_read && destination_write
                    && std::ranges::any_of(
                        process.driver_regions,
                        [&](const auto& region) {
                            return region.signal == destination;
                        });
            });
    };
    assert(find_output_adapter(
        ordinary_left_formal, ordinary_left_output));
    assert(find_output_adapter(
        ordinary_right_formal, ordinary_right_output));
    assert(find_output_adapter(
        ordinary_other_formal, ordinary_other_output));
    verify_writer(*generated_left, generated_left_clock, generated_left_reset,
        signal("generated_left_value"),
        generated_left_lane,
        "static_always_template_top.generated_left.lanes[0]", true);
    verify_writer(*generated_right, generated_right_clock, generated_right_reset,
        signal("generated_right_value"),
        generated_right_lane,
        "static_always_template_top.generated_right.lanes[0]", true);
    verify_writer(*generated_other, generated_other_clock, generated_other_reset,
        signal("generated_other_value"),
        generated_other_lane,
        "static_always_template_top.generated_other.lanes[0]", true);
    verify_writer(*generated_left_one, generated_left_clock, generated_left_reset,
        signal("generated_left_value"),
        generated_left_lane_one,
        "static_always_template_top.generated_left.lanes[1]", true);
    verify_writer(*generated_right_one, generated_right_clock,
        generated_right_reset,
        signal("generated_right_value"),
        generated_right_lane_one,
        "static_always_template_top.generated_right.lanes[1]", true);
    verify_writer(*generated_other_one, generated_other_clock,
        generated_other_reset,
        signal("generated_other_value"),
        generated_other_lane_one,
        "static_always_template_top.generated_other.lanes[1]", true);

    assert(ordinary_left->id != ordinary_right->id);
    assert(ordinary_left->id != ordinary_other->id);
    assert(generated_left->id != generated_right->id);
    assert(generated_left->id != generated_other->id);
    assert(generated_left->id != generated_left_one->id);
    assert(ordinary_left->operations.shares_body_with(
        ordinary_right->operations));
    assert(!ordinary_left->operations.shares_body_with(
        ordinary_other->operations));
    assert(generated_left->operations.shares_body_with(
        generated_right->operations));
    assert(generated_left_one->operations.shares_body_with(
        generated_right_one->operations));
    assert(!generated_left->operations.shares_body_with(
        generated_other->operations));
    assert(!generated_left_one->operations.shares_body_with(
        generated_other_one->operations));
    assert(!ordinary_right->operations.instance_operation_overrides().empty());
    assert(!generated_right->operations.instance_operation_overrides().empty());

    const auto has_semantic_provenance = [&](const auto& process) {
        return std::ranges::count_if(
            elaborated.design->specializations(),
            [&](const auto& specialization) {
                const auto has_process = std::ranges::find(
                    specialization.processes, process.id)
                    != specialization.processes.end();
                const auto provenance_count = std::ranges::count_if(
                    specialization.semantic_processes,
                    [&](const auto& mapping) {
                        return mapping.first == process.id;
                    });
                return has_process && provenance_count == 1;
            }) == 1;
    };
    assert(has_semantic_provenance(*ordinary_left));
    assert(has_semantic_provenance(*ordinary_right));
    assert(has_semantic_provenance(*ordinary_other));
    assert(has_semantic_provenance(*generated_left));
    assert(has_semantic_provenance(*generated_right));
    assert(has_semantic_provenance(*generated_other));
    assert(has_semantic_provenance(*generated_left_one));
    assert(has_semantic_provenance(*generated_right_one));
    assert(has_semantic_provenance(*generated_other_one));

    fsim::diagnostic::Engine artifact_diagnostics;
    const auto encoded = fsim::app::runtime_path_codec::
        serialize_runtime_path_state(
            elaborated.design->state(), nullptr, artifact_diagnostics);
    assert(encoded);
    const auto decoded = fsim::app::runtime_path_codec::
        deserialize_runtime_path_state(
            *encoded, "static-always-template-replay", nullptr,
            artifact_diagnostics);
    assert(decoded);
    assert(decoded->processes.at(ordinary_left->id).operations.shares_body_with(
        decoded->processes.at(ordinary_right->id).operations));
    assert(decoded->processes.at(generated_left->id).operations.shares_body_with(
        decoded->processes.at(generated_right->id).operations));
    assert(decoded->processes.at(generated_left_one->id)
            .operations.shares_body_with(
                decoded->processes.at(generated_right_one->id).operations));
    assert(!decoded->processes.at(ordinary_left->id)
                .operations.shares_body_with(
                    decoded->processes.at(ordinary_other->id).operations));
    assert(!decoded->processes.at(generated_left->id)
                .operations.shares_body_with(
                    decoded->processes.at(generated_other->id).operations));
    assert(decoded->processes.at(ordinary_right->id).name
        == ordinary_right->name);
    assert(decoded->processes.at(ordinary_right->id).id
        == ordinary_right->id);
    assert(std::ranges::any_of(
        decoded->processes.at(ordinary_right->id).static_sensitivity,
        [&](const auto& sensitivity) {
            return sensitivity.signal == ordinary_right_clock
                && sensitivity.edge == EdgeKind::posedge;
        }));
    assert(std::ranges::any_of(
        decoded->processes.at(ordinary_right->id).static_sensitivity,
        [&](const auto& sensitivity) {
            return sensitivity.signal == ordinary_right_reset
                && sensitivity.edge == EdgeKind::negedge;
        }));
    assert(decoded->processes.at(ordinary_right->id)
            .driver_regions.front().signal == ordinary_right_formal);
    assert(decoded->processes.at(generated_right->id).name
        == generated_right->name);
    assert(decoded->processes.at(generated_right->id).id
        == generated_right->id);
    assert(std::ranges::any_of(
        decoded->processes.at(generated_right->id).static_sensitivity,
        [&](const auto& sensitivity) {
            return sensitivity.signal == generated_right_clock
                && sensitivity.edge == EdgeKind::posedge;
        }));
    assert(std::ranges::any_of(
        decoded->processes.at(generated_right->id).static_sensitivity,
        [&](const auto& sensitivity) {
            return sensitivity.signal == generated_right_reset
                && sensitivity.edge == EdgeKind::negedge;
        }));
    assert(decoded->processes.at(generated_right->id)
            .driver_regions.front().signal == generated_right_lane);
    assert(!decoded->processes.at(ordinary_right->id)
                .operations.instance_operation_overrides().empty());
    const auto encoded_again = fsim::app::runtime_path_codec::
        serialize_runtime_path_state(
            *decoded, nullptr, artifact_diagnostics);
    assert(encoded_again && *encoded_again == *encoded);

    const auto initial_state = elaborated.design->state();
    assert(initial_state.signals.at(ordinary_left_output)
            .initial_value.to_msb_string() == "ZZ");
    assert(initial_state.signals.at(ordinary_left_formal)
            .initial_value.to_msb_string() == "XX");
    auto interpreter = elaborated.design->create_interpreter();
    const auto deposit = [&](const SignalId id,
                             const std::string_view value) {
        interpreter->deposit_signal(
            id, PackedLogic4::from_msb_string(value));
    };
    const auto read = [&](const SignalId id) {
        return interpreter->signal_value(id).to_msb_string();
    };
    const auto run = [&] {
        assert(interpreter->run().status
            == fsim::runtime::RunStatus::completed);
    };
    deposit(ordinary_left_clock, "0");
    deposit(ordinary_right_clock, "0");
    deposit(ordinary_other_clock, "0");
    deposit(ordinary_left_reset, "1");
    deposit(ordinary_right_reset, "1");
    deposit(ordinary_other_reset, "1");
    deposit(generated_left_clock, "0");
    deposit(generated_right_clock, "0");
    deposit(generated_other_clock, "0");
    deposit(generated_left_reset, "1");
    deposit(generated_right_reset, "1");
    deposit(generated_other_reset, "1");
    deposit(signal("ordinary_left_value"), "00");
    deposit(signal("ordinary_right_value"), "11");
    deposit(signal("ordinary_other_value"), "10");
    deposit(signal("generated_left_value"), "00");
    deposit(signal("generated_right_value"), "11");
    deposit(signal("generated_other_value"), "10");
    run();
    assert(read(ordinary_left_output) == "XX");
    assert(read(generated_left_output) == "XX");

    deposit(ordinary_left_clock, "1");
    run();
    assert(read(ordinary_left_output) == "01");
    assert(read(ordinary_right_output) == "XX");
    assert(read(ordinary_other_output) == "XX");
    assert(read(generated_left_output) == "XX");
    deposit(generated_left_clock, "1");
    run();
    assert(read(generated_left_output) == "11");
    assert(read(generated_right_output) == "XX");
    assert(read(generated_other_output) == "XX");

    deposit(ordinary_left_clock, "0");
    deposit(generated_left_clock, "0");
    deposit(signal("ordinary_left_value"), "1X");
    deposit(signal("generated_left_value"), "Z1");
    run();
    assert(read(ordinary_left_output) == "01");
    assert(read(generated_left_output) == "11");

    deposit(ordinary_left_reset, "0");
    deposit(generated_left_reset, "0");
    run();
    assert(read(ordinary_left_output) == "1X");
    assert(read(generated_left_output) == "X0");
    assert(read(ordinary_right_output) == "XX");
    assert(read(generated_right_output) == "XX");

    deposit(ordinary_left_reset, "1");
    deposit(generated_left_reset, "1");
    deposit(signal("ordinary_left_value"), "00");
    deposit(signal("generated_left_value"), "10");
    run();
    assert(read(ordinary_left_output) == "1X");
    assert(read(generated_left_output) == "X0");
    deposit(ordinary_left_clock, "1");
    deposit(generated_left_clock, "1");
    run();
    assert(read(ordinary_left_output) == "01");
    assert(read(generated_left_output) == "01");

    deposit(signal("ordinary_right_value"), "Z0");
    deposit(signal("generated_right_value"), "0Z");
    deposit(signal("ordinary_other_value"), "0Z");
    deposit(signal("generated_other_value"), "1X");
    run();
    assert(read(ordinary_right_output) == "XX");
    assert(read(generated_right_output) == "XX");
    assert(read(ordinary_other_output) == "XX");
    assert(read(generated_other_output) == "XX");

    deposit(ordinary_right_reset, "0");
    deposit(generated_right_reset, "0");
    run();
    assert(read(ordinary_right_output) == "X1");
    assert(read(generated_right_output) == "1X");
    assert(read(ordinary_other_output) == "XX");
    assert(read(generated_other_output) == "XX");

    deposit(ordinary_other_reset, "0");
    deposit(generated_other_reset, "0");
    run();
    assert(read(ordinary_other_output) == "0X");
    assert(read(generated_other_output) == "1X");
}

void test_output_variable_actual_supported_custom_and_uwire_nets()
{
    using fsim::runtime::PackedLogic4;
    using fsim::runtime::simir::Process;
    using fsim::runtime::simir::ResolutionKind;
    using fsim::runtime::simir::SignalId;

    const auto parsed = fsim::frontend::parse_text(
        "sv-output-variable-user-nettypes.sv",
        R"(
package output_actual_net_pkg;
  function automatic logic [3:0] first_driver(
      input logic [3:0] drivers[]);
    return drivers[0];
  endfunction
  nettype logic [3:0] first_net with first_driver;
endpackage

module output_actual_net_leaf (
  clock, data, custom_value, uwire_value
);
  input logic clock;
  input logic data;
  output logic [3:0] custom_value = 4'h5;
  output logic [3:0] uwire_value = 4'h6;

  always @(posedge clock) begin
    custom_value <= {3'b101, data};
    uwire_value <= {3'b010, data};
  end
endmodule

module output_actual_net_top;
  import output_actual_net_pkg::*;
  logic clock;
  logic data;
  logic user_driver;
  first_net custom_actual;
  uwire [3:0] uwire_actual;

  assign custom_actual = user_driver;
  output_actual_net_leaf child (
    .clock(clock), .data(data),
    .custom_value(custom_actual), .uwire_value(uwire_actual)
  );
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

    const auto elaborated = compile_and_elaborate(
        parsed.design, "sv:work.output_actual_net_top");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok() && elaborated.design);

    const auto signal = [&](const std::string_view name) {
        const auto id = elaborated.design->find_signal(
            "output_actual_net_top." + std::string { name });
        assert(id);
        return *id;
    };
    const auto custom_actual = signal("custom_actual");
    const auto uwire_actual = signal("uwire_actual");
    const auto custom_formal = signal("child.custom_value");
    const auto uwire_formal = signal("child.uwire_value");
    const auto initial_state = elaborated.design->state();
    assert(custom_actual != custom_formal);
    assert(uwire_actual != uwire_formal);
    assert(initial_state.signals.at(custom_actual).resolution
        == ResolutionKind::sv_user_first);
    assert(initial_state.signals.at(uwire_actual).resolution
        == ResolutionKind::none);
    assert(initial_state.signals.at(custom_actual).initial_value
            .to_msb_string() == "ZZZZ");
    assert(initial_state.signals.at(uwire_actual).initial_value
            .to_msb_string() == "ZZZZ");
    assert(initial_state.signals.at(custom_formal).initial_value
            .to_msb_string() == "XXXX");
    assert(initial_state.signals.at(uwire_formal).initial_value
            .to_msb_string() == "XXXX");

    const auto has_driver = [&](const Process& process,
                                const SignalId destination) {
        return std::ranges::any_of(
            process.driver_regions,
            [&](const auto& region) {
                return region.signal == destination;
            });
    };
    std::vector<fsim::runtime::simir::ProcessId> custom_drivers;
    std::size_t uwire_driver_count { };
    std::size_t custom_initializer_count { };
    std::size_t uwire_initializer_count { };
    for (const auto& process : elaborated.design->processes()) {
        if (has_driver(process, custom_actual)) {
            custom_drivers.push_back(process.id);
        }
        if (has_driver(process, uwire_actual)) {
            ++uwire_driver_count;
        }
        if (process.name.find("$declaration_initializer_custom_value")
                != std::string::npos
            && has_driver(process, custom_formal)) {
            ++custom_initializer_count;
        }
        if (process.name.find("$declaration_initializer_uwire_value")
                != std::string::npos
            && has_driver(process, uwire_formal)) {
            ++uwire_initializer_count;
        }
    }
    assert(custom_drivers.size() == 2U);
    assert(uwire_driver_count == 1U);
    assert(custom_initializer_count == 1U);
    assert(uwire_initializer_count == 1U);
    const auto first_custom_driver
        = *std::ranges::min_element(custom_drivers);

    auto interpreter = elaborated.design->create_interpreter();
    const auto deposit = [&](const SignalId id,
                             const std::string_view value) {
        interpreter->deposit_signal(
            id, PackedLogic4::from_msb_string(value));
    };
    const auto read = [&](const SignalId id) {
        return interpreter->signal_value(id).to_msb_string();
    };
    deposit(signal("clock"), "0");
    deposit(signal("data"), "0");
    deposit(signal("user_driver"), "0");
    assert(interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(read(custom_formal) == "0101");
    assert(read(uwire_formal) == "0110");
    assert(read(uwire_actual) == "0110");

    deposit(signal("data"), "1");
    deposit(signal("clock"), "1");
    assert(interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(read(custom_formal) == "1011");
    assert(read(uwire_formal) == "0101");
    assert(read(uwire_actual) == "0101");
    const auto first_driver_value = interpreter->driver_value(
        first_custom_driver, custom_actual).to_msb_string();
    assert(first_driver_value == "0000"
        || first_driver_value == "1011");
    assert(read(custom_actual) == first_driver_value);

    const auto invalid = fsim::frontend::parse_text(
        "sv-uwire-output-variable-multiple-drivers.sv",
        R"(
module uwire_output_leaf(output logic result);
  always_comb result = 1'b1;
endmodule

module uwire_multiple_driver_top;
  logic parent_value;
  uwire actual;
  assign actual = parent_value;
  uwire_output_leaf child (.result(actual));
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid.ok());
    const auto rejected = compile_and_elaborate(
        invalid.design, "sv:work.uwire_multiple_driver_top");
    assert(!rejected.ok());
    assert(std::ranges::any_of(
        rejected.diagnostics,
        [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-DRV-001";
        }));
}

void test_output_variable_actual_resolved_net()
{
    using fsim::runtime::PackedLogic4;
    using fsim::runtime::simir::Process;
    using fsim::runtime::simir::SignalId;

    const auto parsed = fsim::frontend::parse_text(
        "sv-output-variable-actual-resolved-net.sv",
        R"(
module output_variable_leaf (
  clock, data, logic_default, logic_initialized, bit_default
);
  input logic clock;
  input logic data;
  output logic logic_default;
  output logic logic_initialized = 1'b1;
  output bit bit_default;

  always @(posedge clock) begin
    logic_default <= data;
    logic_initialized <= ~data;
    bit_default <= data;
  end
endmodule

module output_variable_actual_top;
  logic clock;
  logic data;
  logic second_driver;
  wire logic_default_actual;
  wire logic_initialized_actual;
  wire bit_default_actual;

  assign logic_default_actual = second_driver;
  assign logic_initialized_actual = second_driver;
  assign bit_default_actual = second_driver;

  output_variable_leaf child (
    .clock(clock),
    .data(data),
    .logic_default(logic_default_actual),
    .logic_initialized(logic_initialized_actual),
    .bit_default(bit_default_actual)
  );
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

    const auto elaborated = compile_and_elaborate(
        parsed.design, "sv:work.output_variable_actual_top");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok() && elaborated.design);

    const auto signal = [&](const std::string_view name) {
        const auto id = elaborated.design->find_signal(
            "output_variable_actual_top." + std::string { name });
        assert(id);
        return *id;
    };
    const auto clock = signal("clock");
    const auto data = signal("data");
    const auto second_driver = signal("second_driver");
    const auto logic_actual = signal("logic_default_actual");
    const auto initialized_actual = signal("logic_initialized_actual");
    const auto bit_actual = signal("bit_default_actual");
    const auto logic_formal = signal("child.logic_default");
    const auto initialized_formal = signal("child.logic_initialized");
    const auto bit_formal = signal("child.bit_default");

    const auto initial_state = elaborated.design->state();
    assert(initial_state.signals.at(logic_actual)
            .initial_value.to_msb_string() == "Z");
    assert(initial_state.signals.at(initialized_actual)
            .initial_value.to_msb_string() == "Z");
    assert(initial_state.signals.at(bit_actual)
            .initial_value.to_msb_string() == "Z");
    assert(initial_state.signals.at(logic_formal)
            .initial_value.to_msb_string() == "X");
    assert(initial_state.signals.at(bit_formal)
            .initial_value.to_msb_string() == "0");

    const auto has_driver = [&](const Process& process,
                                const SignalId destination) {
        return std::ranges::any_of(
            process.driver_regions,
            [&](const auto& region) {
                return region.signal == destination;
            });
    };
    const auto driver_count = [&](const SignalId destination) {
        return std::ranges::count_if(
            elaborated.design->processes(),
            [&](const Process& process) {
                return has_driver(process, destination);
            });
    };
    assert(driver_count(logic_actual) == 2);
    assert(driver_count(initialized_actual) == 2);
    assert(driver_count(bit_actual) == 2);

    auto interpreter = elaborated.design->create_interpreter();
    const auto deposit = [&](const SignalId id,
                             const std::string_view value) {
        interpreter->deposit_signal(
            id, PackedLogic4::from_msb_string(value));
    };
    const auto read = [&](const SignalId id) {
        return interpreter->signal_value(id).to_msb_string();
    };
    const auto run = [&] {
        assert(interpreter->run().status
            == fsim::runtime::RunStatus::completed);
    };

    deposit(clock, "0");
    deposit(data, "0");
    deposit(second_driver, "Z");
    run();
    assert(read(logic_actual) == "X");
    assert(read(initialized_actual) == "1");
    assert(read(bit_actual) == "0");
    assert(read(logic_formal) == "X");
    assert(read(initialized_formal) == "1");
    assert(read(bit_formal) == "0");

    deposit(data, "1");
    deposit(clock, "1");
    run();
    assert(read(logic_actual) == "1");
    assert(read(initialized_actual) == "0");
    assert(read(bit_actual) == "1");
    assert(read(logic_formal) == "1");
    assert(read(initialized_formal) == "0");
    assert(read(bit_formal) == "1");

    deposit(second_driver, "0");
    run();
    assert(read(logic_actual) == "X");
    assert(read(initialized_actual) == "0");
    assert(read(bit_actual) == "X");
    assert(read(logic_formal) == "1");
    assert(read(initialized_formal) == "0");
    assert(read(bit_formal) == "1");

    deposit(second_driver, "Z");
    run();
    assert(read(logic_actual) == "1");
    assert(read(initialized_actual) == "0");
    assert(read(bit_actual) == "1");
    test_output_variable_actual_supported_custom_and_uwire_nets();
}

void test_timed_always_template_remains_uncached()
{
    using fsim::runtime::simir::Process;

    const auto parsed = fsim::frontend::parse_text(
        "sv-timed-always-template-fallback.sv",
        R"(
module timed_always_leaf (
  input logic value,
  output logic result
);
  always begin
    #1 result = value;
  end
endmodule

module timed_always_template_top;
  logic left_value;
  logic right_value;
  wire left_result;
  wire right_result;
  timed_always_leaf left (.value(left_value), .result(left_result));
  timed_always_leaf right (.value(right_value), .result(right_result));
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());

    std::ostringstream profile_output;
    fsim::elaboration::ElaborationResult elaborated;
    {
        const ProfileCapture profile { profile_output };
        elaborated = compile_and_elaborate(
            parsed.design, "sv:work.timed_always_template_top");
    }
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok() && elaborated.design);
    const auto output = profile_output.str();
    const auto row_start = output.find(
        "fsim-profile: systemverilog-ordinary-process-template ");
    assert(row_start != std::string::npos);
    const auto row_end = output.find('\n', row_start);
    const auto row = std::string_view { output }.substr(
        row_start, row_end == std::string::npos
            ? output.size() - row_start : row_end - row_start);
    assert(profile_metric(row, "occurrences") == 2U);
    assert(profile_metric(row, "lowerer_calls") == 2U);
    assert(profile_metric(row, "cached_programs") == 0U);
    assert(profile_metric(row, "replays") == 0U);

    const auto find_process = [&](const std::string_view suffix) {
        const auto id = elaborated.design->find_signal(
            "timed_always_template_top." + std::string { suffix });
        assert(id);
        for (const auto& process : elaborated.design->processes()) {
            if (std::ranges::any_of(process.driver_regions,
                    [&](const auto& driver) { return driver.signal == *id; })) {
                return &process;
            }
        }
        return static_cast<const Process*>(nullptr);
    };
    const auto* left = find_process("left.$actual_result");
    const auto* right = find_process("right.$actual_result");
    const auto left_formal = elaborated.design->find_signal(
        "timed_always_template_top.left.result");
    const auto left_backing = elaborated.design->find_signal(
        "timed_always_template_top.left.$actual_result");
    const auto right_formal = elaborated.design->find_signal(
        "timed_always_template_top.right.result");
    const auto right_backing = elaborated.design->find_signal(
        "timed_always_template_top.right.$actual_result");
    assert(left_formal && left_backing && *left_formal == *left_backing);
    assert(right_formal && right_backing && *right_formal == *right_backing);
    assert(left && right && left->id != right->id);
    assert(left->static_sensitivity.empty());
    assert(right->static_sensitivity.empty());
}

} // namespace

void test_systemverilog_generated_process_template_replay()
{
    using fsim::runtime::simir::Process;
    using fsim::runtime::simir::SignalId;

    const auto parsed = fsim::frontend::parse_text(
        "sv-generated-process-template-replay.sv",
        R"(
module process_template_leaf #(
  parameter logic INVERT = 1'b1
) (
  input logic [1:0] value,
  output wire [1:0] transformed
);
  for (genvar index = 0; index < 2; ++index) begin : lanes
    logic lane_value;
    always_comb lane_value = value[index] ^ INVERT;
    assign transformed[index] = lane_value;
  end
endmodule

module process_template_top;
  logic [1:0] left_value;
  logic [1:0] right_value;
  logic [1:0] other_value;
  logic [1:0] left_result;
  logic [1:0] right_result;
  logic [1:0] other_result;
  process_template_leaf #(.INVERT(1'b1)) left (
    .value(left_value), .transformed(left_result));
  process_template_leaf #(.INVERT(1'b1)) right (
    .value(right_value), .transformed(right_result));
  process_template_leaf #(.INVERT(1'b0)) other (
    .value(other_value), .transformed(other_result));
  initial begin
    left_value = 2'b00;
    right_value = 2'b11;
    other_value = 2'b10;
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
            parsed.design, "sv:work.process_template_top");
    }
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok() && elaborated.design);

    const auto output = profile_output.str();
    const auto row_start = output.find(
        "fsim-profile: systemverilog-generated-process-template ");
    assert(row_start != std::string::npos);
    const auto row_end = output.find('\n', row_start);
    const auto row = std::string_view { output }.substr(
        row_start,
        row_end == std::string::npos
            ? output.size() - row_start
            : row_end - row_start);
    const auto occurrences = profile_metric(row, "occurrences");
    const auto lowerer_calls = profile_metric(row, "lowerer_calls");
    const auto replays = profile_metric(row, "replays");
    assert(occurrences == 6U);
    assert(lowerer_calls + replays == occurrences);
    assert(profile_metric(row, "cached_programs") >= 2U);
    assert(replays >= 1U);

    const auto find_process = [&](const std::string_view path)
        -> const Process* {
        const auto found = std::ranges::find_if(
            elaborated.design->processes(),
            [&](const Process& process) {
                return process.name == path;
            });
        return found == elaborated.design->processes().end()
            ? nullptr : &*found;
    };
    const auto* left_zero
        = find_process("process_template_top.left.lanes[0]");
    const auto* left_one
        = find_process("process_template_top.left.lanes[1]");
    const auto* right_zero
        = find_process("process_template_top.right.lanes[0]");
    const auto* right_one
        = find_process("process_template_top.right.lanes[1]");
    assert(left_zero && left_one && right_zero && right_one);
    assert(left_zero->id != right_zero->id);
    assert(left_one->id != right_one->id);
    assert(left_zero->operations.shares_body_with(right_zero->operations));
    assert(left_one->operations.shares_body_with(right_one->operations));
    assert(!left_zero->register_value_kinds.empty());
    assert(left_zero->register_value_kinds.shares_storage_with(
        right_zero->register_value_kinds));
    const auto left_kinds = fsim::runtime::simir::process_layout_detail::
        ProcessLayoutAccess::view(left_zero->register_value_kinds);
    const auto right_kinds = fsim::runtime::simir::process_layout_detail::
        ProcessLayoutAccess::view(right_zero->register_value_kinds);
    assert(left_kinds.vector() == right_kinds.vector());
    if (!left_zero->static_trigger_regions.empty()) {
        assert(left_zero->static_trigger_regions.shares_storage_with(
            right_zero->static_trigger_regions));
    }
    assert(!right_zero->operations.instance_operation_overrides().empty());
    assert(!right_one->operations.instance_operation_overrides().empty());
    assert(left_zero->static_sensitivity.size() == 1U);
    assert(right_zero->static_sensitivity.size() == 1U);
    assert(left_zero->static_sensitivity.front().signal
        != right_zero->static_sensitivity.front().signal);
    assert(left_zero->driver_regions.size() == 1U);
    assert(right_zero->driver_regions.size() == 1U);
    assert(left_zero->driver_regions.front().signal
        != right_zero->driver_regions.front().signal);
    assert(left_zero->initialize && right_zero->initialize);
    assert(!left_zero->observed && !right_zero->observed);
    assert(!left_zero->reactive && !right_zero->reactive);
    assert(!left_zero->program_owner && !right_zero->program_owner);

    auto isolated_binding = *right_zero;
    bool changed_read_binding { };
    std::size_t changed_read_index { };
    for (std::size_t index = 0;
         index < isolated_binding.operations.size(); ++index) {
        auto operation = isolated_binding.operations.expanded(index);
        auto* read = fsim::runtime::simir::operation_get_if<
            fsim::runtime::simir::ReadSignal>(&operation);
        if (read == nullptr) {
            continue;
        }
        read->signal = right_zero->driver_regions.front().signal;
        isolated_binding.operations.replace(index, std::move(operation));
        changed_read_index = index;
        changed_read_binding = true;
        break;
    }
    assert(changed_read_binding);
    assert(isolated_binding.operations.shares_body_with(
        right_zero->operations));
    const auto isolated_operation
        = isolated_binding.operations.expanded(changed_read_index);
    const auto original_operation
        = right_zero->operations.expanded(changed_read_index);
    const auto* isolated_read = fsim::runtime::simir::operation_get_if<
        fsim::runtime::simir::ReadSignal>(&isolated_operation);
    const auto* original_read = fsim::runtime::simir::operation_get_if<
        fsim::runtime::simir::ReadSignal>(&original_operation);
    assert(isolated_read && original_read);
    assert(isolated_read->signal == right_zero->driver_regions.front().signal);
    assert(original_read->signal != isolated_read->signal);

    auto artifact_diagnostics = fsim::diagnostic::Engine { };
    const auto runtime_state = elaborated.design->state();
    const auto encoded = fsim::app::runtime_path_codec::
        serialize_runtime_path_state(
            runtime_state, nullptr, artifact_diagnostics);
    assert(encoded);
    auto decoded = fsim::app::runtime_path_codec::
        deserialize_runtime_path_state(
            *encoded, "generated-process-template-replay", nullptr,
            artifact_diagnostics);
    assert(decoded);
    const auto find_decoded_process = [&](const std::string_view name)
        -> const Process* {
        const auto found = std::ranges::find_if(
            decoded->processes,
            [&](const Process& process) { return process.name == name; });
        return found == decoded->processes.end() ? nullptr : &*found;
    };
    const auto* decoded_left
        = find_decoded_process("process_template_top.left.lanes[0]");
    const auto* decoded_right
        = find_decoded_process("process_template_top.right.lanes[0]");
    assert(decoded_left && decoded_right);
    assert(decoded_left->operations.shares_body_with(
        decoded_right->operations));
    assert(decoded_left->register_value_kinds
        == left_zero->register_value_kinds);
    assert(decoded_right->register_value_kinds
        == right_zero->register_value_kinds);
    assert(!decoded_right->operations.instance_operation_overrides().empty());
    const auto left_input
        = elaborated.design->find_signal("process_template_top.left_value");
    const auto right_input
        = elaborated.design->find_signal("process_template_top.right_value");
    const auto left_output
        = elaborated.design->find_signal("process_template_top.left_result");
    const auto right_output
        = elaborated.design->find_signal("process_template_top.right_result");
    const auto left_lane = elaborated.design->find_signal(
        "process_template_top.left.lanes[0].lane_value");
    const auto right_lane = elaborated.design->find_signal(
        "process_template_top.right.lanes[0].lane_value");
    assert(left_input && right_input && left_output && right_output
        && left_lane && right_lane);
    const auto assert_decoded_bindings = [&](const Process& process,
                                             const SignalId input,
                                             const SignalId output) {
        bool reads_input { };
        bool writes_output { };
        for (std::size_t index = 0U;
             index < process.operations.size(); ++index) {
            const auto operation = process.operations.expanded(index);
            if (const auto* read = fsim::runtime::simir::operation_get_if<
                    fsim::runtime::simir::ReadSignal>(&operation)) {
                reads_input = reads_input || read->signal == input;
            }
            const auto check_write = [&](const auto* write) {
                if (write != nullptr) {
                    writes_output = writes_output
                        || write->signal == output;
                }
            };
            check_write(fsim::runtime::simir::operation_get_if<
                fsim::runtime::simir::WriteBlocking>(&operation));
            check_write(fsim::runtime::simir::operation_get_if<
                fsim::runtime::simir::WriteUpdate>(&operation));
            check_write(fsim::runtime::simir::operation_get_if<
                fsim::runtime::simir::WriteBlockingSlice>(&operation));
            check_write(fsim::runtime::simir::operation_get_if<
                fsim::runtime::simir::WriteUpdateSlice>(&operation));
        }
        assert(reads_input && writes_output);
    };
    assert_decoded_bindings(*decoded_left, *left_input, *left_lane);
    assert_decoded_bindings(*decoded_right, *right_input, *right_lane);
    const auto encoded_again = fsim::app::runtime_path_codec::
        serialize_runtime_path_state(
            *decoded, nullptr, artifact_diagnostics);
    assert(encoded_again && *encoded_again == *encoded);

    const auto left_result
        = elaborated.design->find_signal("process_template_top.left_result");
    const auto right_result
        = elaborated.design->find_signal("process_template_top.right_result");
    const auto other_result
        = elaborated.design->find_signal("process_template_top.other_result");
    assert(left_result && right_result && other_result);
    auto interpreter = elaborated.design->create_interpreter();
    assert(interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(interpreter->signal_value(*left_result).to_msb_string() == "11");
    assert(interpreter->signal_value(*right_result).to_msb_string() == "00");
    assert(interpreter->signal_value(*other_result).to_msb_string() == "10");
}

void test_systemverilog_ordinary_process_template_replay()
{
    using fsim::runtime::simir::Process;

    const auto parsed = fsim::frontend::parse_text(
        "sv-ordinary-process-template-replay.sv",
        R"(
module process_template_leaf #(
  parameter logic INVERT = 1'b1
) (
  input logic [1:0] value,
  output logic [1:0] transformed
);
  always_comb transformed = value ^ INVERT;
endmodule

module process_template_top;
  logic [1:0] left_value;
  logic [1:0] right_value;
  logic [1:0] other_value;
  logic [1:0] left_result;
  logic [1:0] right_result;
  logic [1:0] other_result;
  process_template_leaf #(.INVERT(1'b1)) left (
    .value(left_value), .transformed(left_result));
  process_template_leaf #(.INVERT(1'b1)) right (
    .value(right_value), .transformed(right_result));
  process_template_leaf #(.INVERT(1'b0)) other (
    .value(other_value), .transformed(other_result));
  initial begin
    left_value = 2'b00;
    right_value = 2'b11;
    other_value = 2'b10;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());

    std::ostringstream profile_output;
    fsim::elaboration::ElaborationResult elaborated;
    {
        const ProfileCapture profile { profile_output };
        elaborated = compile_and_elaborate(
            parsed.design, "sv:work.process_template_top");
    }
    assert(elaborated.ok() && elaborated.design);
    const auto output = profile_output.str();
    const auto row_start = output.find(
        "fsim-profile: systemverilog-ordinary-process-template ");
    assert(row_start != std::string::npos);
    const auto row_end = output.find('\n', row_start);
    const auto row = std::string_view { output }.substr(
        row_start,
        row_end == std::string::npos
            ? output.size() - row_start
            : row_end - row_start);
    const auto occurrences = profile_metric(row, "occurrences");
    const auto lowerer_calls = profile_metric(row, "lowerer_calls");
    const auto replays = profile_metric(row, "replays");
    assert(occurrences == 4U);
    assert(lowerer_calls + replays == occurrences);
    assert(profile_metric(row, "cached_programs") == 2U);
    assert(replays == 1U);

    using ProcessAccess
        = fsim::elaboration::detail::ElaboratedDesignProcessAccess;
    const auto process_table = ProcessAccess::process_table(
        *elaborated.design);
    assert(ProcessAccess::row_backed(*elaborated.design));
    assert(process_table != nullptr);
    assert(process_table->rows.size()
        == elaborated.design->process_count());
    std::optional<std::size_t> left_row;
    std::optional<std::size_t> right_row;
    for (std::size_t index = 0U;
        index < elaborated.design->process_count(); ++index) {
        const auto process = ProcessAccess::process_view(
            *elaborated.design, index);
        if (process.name().starts_with("process_template_top.left.process_")) {
            left_row = index;
        } else if (process.name().starts_with(
                       "process_template_top.right.process_")) {
            right_row = index;
        }
    }
    assert(left_row && right_row);
    assert(process_table->rows[*left_row].template_id
        == process_table->rows[*right_row].template_id);
    const auto left_row_view = ProcessAccess::process_view(
        *elaborated.design, *left_row);
    const auto right_row_view = ProcessAccess::process_view(
        *elaborated.design, *right_row);
    assert(left_row_view.common_identity()
        == right_row_view.common_identity());
    assert(left_row_view.operations().shares_body_with(
        right_row_view.operations()));
    assert(ProcessAccess::row_backed(*elaborated.design));

    const auto left_input
        = elaborated.design->find_signal("process_template_top.left_value");
    const auto right_input
        = elaborated.design->find_signal("process_template_top.right_value");
    const auto other_input
        = elaborated.design->find_signal("process_template_top.other_value");
    const auto left_output
        = elaborated.design->find_signal("process_template_top.left_result");
    const auto right_output
        = elaborated.design->find_signal("process_template_top.right_result");
    const auto other_output
        = elaborated.design->find_signal("process_template_top.other_result");
    assert(left_input && right_input && other_input
        && left_output && right_output && other_output);
    const auto find_assignment = [&](const fsim::runtime::simir::SignalId input,
                                     const fsim::runtime::simir::SignalId output)
        -> const Process* {
        for (const auto& process : elaborated.design->processes()) {
            bool reads_input { };
            bool writes_output { };
            for (std::size_t index = 0U;
                 index < process.operations.size(); ++index) {
                const auto operation = process.operations.expanded(index);
                if (const auto* read =
                        fsim::runtime::simir::operation_get_if<
                            fsim::runtime::simir::ReadSignal>(&operation)) {
                    reads_input = reads_input || read->signal == input;
                }
                const auto check_write = [&](const auto* write) {
                    if (write != nullptr) {
                        writes_output = writes_output
                            || write->signal == output;
                    }
                };
                check_write(fsim::runtime::simir::operation_get_if<
                    fsim::runtime::simir::WriteBlocking>(&operation));
                check_write(fsim::runtime::simir::operation_get_if<
                    fsim::runtime::simir::WriteUpdate>(&operation));
                check_write(fsim::runtime::simir::operation_get_if<
                    fsim::runtime::simir::WriteBlockingSlice>(&operation));
                check_write(fsim::runtime::simir::operation_get_if<
                    fsim::runtime::simir::WriteUpdateSlice>(&operation));
            }
            if (reads_input && writes_output) {
                return &process;
            }
        }
        return nullptr;
    };
    const auto* left = find_assignment(*left_input, *left_output);
    const auto* right = find_assignment(*right_input, *right_output);
    const auto* other = find_assignment(*other_input, *other_output);
    assert(left && right && other);
    assert(!ProcessAccess::row_backed(*elaborated.design));
    assert(left->id != right->id && left->name != right->name);
    assert(left->name == "process_template_top.left.process_"
            + std::to_string(left->id));
    assert(right->name == "process_template_top.right.process_"
            + std::to_string(right->id));
    assert(left->operations.shares_body_with(right->operations));
    assert(!right->operations.instance_operation_overrides().empty());
    assert(left->initialize && right->initialize && other->initialize);
    assert(!left->observed && !right->observed && !other->observed);

    auto interpreter = elaborated.design->create_interpreter();
    assert(interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(interpreter->signal_value(*left_output).to_msb_string() == "01");
    assert(interpreter->signal_value(*right_output).to_msb_string() == "10");
    assert(interpreter->signal_value(*other_output).to_msb_string() == "10");
}


void test_systemverilog_always_ff_process_template_replay()
{
    using fsim::runtime::PackedLogic4;
    using fsim::runtime::simir::EdgeKind;
    using fsim::runtime::simir::Process;
    using fsim::runtime::simir::SignalId;
    using fsim::runtime::simir::SignalUpdateDomain;

    const auto parsed = fsim::frontend::parse_text(
        "sv-always-ff-process-template-replay.sv",
        R"(
module ordinary_ff_leaf #(
  parameter logic INVERT = 1'b1
) (
  input logic clock,
  input logic [1:0] value,
  output logic [1:0] result
);
  always_ff @(posedge clock) result <= value ^ INVERT;
endmodule

module generated_ff_leaf #(
  parameter logic INVERT = 1'b1
) (
  input logic clock,
  input logic [1:0] value,
  output wire [1:0] result
);
  for (genvar index = 0; index < 2; ++index) begin : lanes
    logic lane_value;
    always_ff @(posedge clock) lane_value <= value[index] ^ INVERT;
    assign result[index] = lane_value;
  end
endmodule

module always_ff_template_top;
  logic clock;
  logic [1:0] ordinary_left_value;
  logic [1:0] ordinary_right_value;
  logic [1:0] ordinary_other_value;
  logic [1:0] ordinary_left_result;
  logic [1:0] ordinary_right_result;
  logic [1:0] ordinary_other_result;
  logic [1:0] generated_left_value;
  logic [1:0] generated_right_value;
  logic [1:0] generated_other_value;
  logic [1:0] generated_left_result;
  logic [1:0] generated_right_result;
  logic [1:0] generated_other_result;

  ordinary_ff_leaf #(.INVERT(1'b1)) ordinary_left (
    .clock(clock), .value(ordinary_left_value),
    .result(ordinary_left_result));
  ordinary_ff_leaf #(.INVERT(1'b1)) ordinary_right (
    .clock(clock), .value(ordinary_right_value),
    .result(ordinary_right_result));
  ordinary_ff_leaf #(.INVERT(1'b0)) ordinary_other (
    .clock(clock), .value(ordinary_other_value),
    .result(ordinary_other_result));

  generated_ff_leaf #(.INVERT(1'b1)) generated_left (
    .clock(clock), .value(generated_left_value),
    .result(generated_left_result));
  generated_ff_leaf #(.INVERT(1'b1)) generated_right (
    .clock(clock), .value(generated_right_value),
    .result(generated_right_result));
  generated_ff_leaf #(.INVERT(1'b0)) generated_other (
    .clock(clock), .value(generated_other_value),
    .result(generated_other_result));
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
            parsed.design, "sv:work.always_ff_template_top");
    }
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok() && elaborated.design);

    const auto output = profile_output.str();
    const auto profile_row = [&](const std::string_view name) {
        const auto marker = std::string { "fsim-profile: " }
            + std::string { name } + " ";
        const auto row_start = output.find(marker);
        assert(row_start != std::string::npos);
        const auto row_end = output.find('\n', row_start);
        return std::string_view { output }.substr(
            row_start,
            row_end == std::string::npos
                ? output.size() - row_start
                : row_end - row_start);
    };
    const auto ordinary_row
        = profile_row("systemverilog-ordinary-process-template");
    assert(profile_metric(ordinary_row, "occurrences") == 3U);
    assert(profile_metric(ordinary_row, "lowerer_calls") == 2U);
    assert(profile_metric(ordinary_row, "cached_programs") == 2U);
    assert(profile_metric(ordinary_row, "replays") == 1U);
    const auto generated_row
        = profile_row("systemverilog-generated-process-template");
    assert(profile_metric(generated_row, "occurrences") == 6U);
    assert(profile_metric(generated_row, "lowerer_calls")
        + profile_metric(generated_row, "replays") == 6U);
    assert(profile_metric(generated_row, "cached_programs") >= 2U);
    assert(profile_metric(generated_row, "replays") >= 1U);

    const auto signal = [&](const std::string_view name) {
        return elaborated.design->find_signal(name);
    };
    const auto clock = signal("always_ff_template_top.clock");
    const auto ordinary_left_input
        = signal("always_ff_template_top.ordinary_left_value");
    const auto ordinary_right_input
        = signal("always_ff_template_top.ordinary_right_value");
    const auto ordinary_other_input
        = signal("always_ff_template_top.ordinary_other_value");
    const auto ordinary_left_output
        = signal("always_ff_template_top.ordinary_left_result");
    const auto ordinary_right_output
        = signal("always_ff_template_top.ordinary_right_result");
    const auto ordinary_other_output
        = signal("always_ff_template_top.ordinary_other_result");
    const auto generated_left_input
        = signal("always_ff_template_top.generated_left_value");
    const auto generated_right_input
        = signal("always_ff_template_top.generated_right_value");
    const auto generated_other_input
        = signal("always_ff_template_top.generated_other_value");
    const auto generated_left_output
        = signal("always_ff_template_top.generated_left_result");
    const auto generated_right_output
        = signal("always_ff_template_top.generated_right_result");
    const auto generated_other_output
        = signal("always_ff_template_top.generated_other_result");
    const auto generated_left_lane
        = signal("always_ff_template_top.generated_left.lanes[0].lane_value");
    const auto generated_right_lane
        = signal("always_ff_template_top.generated_right.lanes[0].lane_value");
    const auto generated_other_lane
        = signal("always_ff_template_top.generated_other.lanes[0].lane_value");
    assert(clock && ordinary_left_input && ordinary_right_input
        && ordinary_other_input && ordinary_left_output
        && ordinary_right_output && ordinary_other_output
        && generated_left_input && generated_right_input
        && generated_other_input && generated_left_output
        && generated_right_output && generated_other_output
        && generated_left_lane && generated_right_lane
        && generated_other_lane);

    const auto find_writer = [&](const SignalId destination)
        -> const Process* {
        for (const auto& process : elaborated.design->processes()) {
            for (std::size_t index = 0U;
                 index < process.operations.size(); ++index) {
                const auto operation = process.operations.expanded(index);
                const auto* update
                    = fsim::runtime::simir::operation_get_if<
                        fsim::runtime::simir::WriteUpdate>(&operation);
                if (update != nullptr && update->signal == destination) {
                    return &process;
                }
            }
        }
        return nullptr;
    };
    const auto verify_writer = [&](const Process& process,
                                   const SignalId input,
                                   const SignalId destination) {
        assert(process.initialize);
        assert(process.static_sensitivity.size() == 1U);
        assert(process.static_sensitivity.front().signal == *clock);
        assert(process.static_sensitivity.front().edge == EdgeKind::posedge);
        assert(process.driver_regions.size() == 1U);
        assert(process.driver_regions.front().signal == destination);
        bool wait_seen { };
        bool input_read { };
        bool nonblocking_write { };
        for (std::size_t index = 0U;
             index < process.operations.size(); ++index) {
            const auto operation = process.operations.expanded(index);
            wait_seen = wait_seen
                || fsim::runtime::simir::operation_holds<
                    fsim::runtime::simir::WaitSensitivity>(operation);
            if (const auto* read = fsim::runtime::simir::operation_get_if<
                    fsim::runtime::simir::ReadSignal>(&operation)) {
                input_read = input_read || read->signal == input;
            }
            if (const auto* update = fsim::runtime::simir::operation_get_if<
                    fsim::runtime::simir::WriteUpdate>(&operation)) {
                nonblocking_write = nonblocking_write
                    || (update->signal == destination
                        && update->domain
                            == SignalUpdateDomain::systemverilog_nba);
            }
        }
        assert(wait_seen && input_read && nonblocking_write);
    };

    const auto* ordinary_left = find_writer(*ordinary_left_output);
    const auto* ordinary_right = find_writer(*ordinary_right_output);
    const auto* ordinary_other = find_writer(*ordinary_other_output);
    const auto* generated_left = find_writer(*generated_left_lane);
    const auto* generated_right = find_writer(*generated_right_lane);
    const auto* generated_other = find_writer(*generated_other_lane);
    assert(ordinary_left && ordinary_right && ordinary_other
        && generated_left && generated_right && generated_other);
    verify_writer(*ordinary_left, *ordinary_left_input, *ordinary_left_output);
    verify_writer(*ordinary_right, *ordinary_right_input, *ordinary_right_output);
    verify_writer(*ordinary_other, *ordinary_other_input, *ordinary_other_output);
    verify_writer(*generated_left, *generated_left_input, *generated_left_lane);
    verify_writer(*generated_right, *generated_right_input, *generated_right_lane);
    verify_writer(*generated_other, *generated_other_input, *generated_other_lane);

    assert(ordinary_left->operations.shares_body_with(
        ordinary_right->operations));
    assert(!ordinary_left->operations.shares_body_with(
        ordinary_other->operations));
    assert(generated_left->operations.shares_body_with(
        generated_right->operations));
    assert(!generated_left->operations.shares_body_with(
        generated_other->operations));
    assert(!ordinary_right->operations.instance_operation_overrides().empty());
    assert(!generated_right->operations.instance_operation_overrides().empty());

    auto artifact_diagnostics = fsim::diagnostic::Engine { };
    const auto state = elaborated.design->state();
    const auto encoded = fsim::app::runtime_path_codec::
        serialize_runtime_path_state(state, nullptr, artifact_diagnostics);
    assert(encoded);
    const auto decoded = fsim::app::runtime_path_codec::
        deserialize_runtime_path_state(
            *encoded, "always-ff-process-template-replay", nullptr,
            artifact_diagnostics);
    assert(decoded);
    assert(decoded->processes.at(ordinary_left->id).operations.shares_body_with(
        decoded->processes.at(ordinary_right->id).operations));
    assert(decoded->processes.at(generated_left->id).operations.shares_body_with(
        decoded->processes.at(generated_right->id).operations));
    assert(!decoded->processes.at(ordinary_right->id)
                .operations.instance_operation_overrides().empty());
    assert(!decoded->processes.at(generated_right->id)
                .operations.instance_operation_overrides().empty());
    const auto encoded_again = fsim::app::runtime_path_codec::
        serialize_runtime_path_state(
            *decoded, nullptr, artifact_diagnostics);
    assert(encoded_again && *encoded_again == *encoded);

    auto interpreter = elaborated.design->create_interpreter();
    const auto deposit = [&](const SignalId id, const std::string_view value) {
        interpreter->deposit_signal(id, PackedLogic4::from_msb_string(value));
    };
    deposit(*clock, "0");
    deposit(*ordinary_left_input, "00");
    deposit(*ordinary_right_input, "11");
    deposit(*ordinary_other_input, "10");
    deposit(*generated_left_input, "00");
    deposit(*generated_right_input, "11");
    deposit(*generated_other_input, "10");
    const auto read = [&](const SignalId id) {
        return interpreter->signal_value(id).to_msb_string();
    };
    assert(interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(read(*ordinary_left_output) == "XX");
    assert(read(*ordinary_right_output) == "XX");
    assert(read(*ordinary_other_output) == "XX");
    assert(read(*generated_left_output) == "XX");
    assert(read(*generated_right_output) == "XX");
    assert(read(*generated_other_output) == "XX");

    deposit(*clock, "1");
    assert(interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(read(*ordinary_left_output) == "01");
    assert(read(*ordinary_right_output) == "10");
    assert(read(*ordinary_other_output) == "10");
    assert(read(*generated_left_output) == "11");
    assert(read(*generated_right_output) == "00");
    assert(read(*generated_other_output) == "10");

    deposit(*ordinary_left_input, "11");
    deposit(*ordinary_right_input, "00");
    deposit(*ordinary_other_input, "01");
    deposit(*generated_left_input, "11");
    deposit(*generated_right_input, "00");
    deposit(*generated_other_input, "01");
    assert(interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(read(*ordinary_left_output) == "01");
    assert(read(*ordinary_right_output) == "10");
    assert(read(*ordinary_other_output) == "10");
    assert(read(*generated_left_output) == "11");
    assert(read(*generated_right_output) == "00");
    assert(read(*generated_other_output) == "10");

    deposit(*clock, "0");
    assert(interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(read(*ordinary_left_output) == "01");
    assert(read(*ordinary_right_output) == "10");
    assert(read(*ordinary_other_output) == "10");
    assert(read(*generated_left_output) == "11");
    assert(read(*generated_right_output) == "00");
    assert(read(*generated_other_output) == "10");

    deposit(*ordinary_left_input, "10");
    deposit(*ordinary_right_input, "01");
    deposit(*ordinary_other_input, "00");
    deposit(*generated_left_input, "10");
    deposit(*generated_right_input, "01");
    deposit(*generated_other_input, "00");
    assert(interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(read(*ordinary_left_output) == "01");
    assert(read(*ordinary_right_output) == "10");
    assert(read(*ordinary_other_output) == "10");
    assert(read(*generated_left_output) == "11");
    assert(read(*generated_right_output) == "00");
    assert(read(*generated_other_output) == "10");

    deposit(*clock, "1");
    assert(interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(read(*ordinary_left_output) == "11");
    assert(read(*ordinary_right_output) == "00");
    assert(read(*ordinary_other_output) == "00");
    assert(read(*generated_left_output) == "01");
    assert(read(*generated_right_output) == "10");
    assert(read(*generated_other_output) == "00");

    test_static_always_template_replay();
    test_output_variable_actual_resolved_net();
    test_timed_always_template_remains_uncached();
}

void test_systemverilog_element_process_template_replay()
{
    using fsim::runtime::PackedLogic4;
    using namespace fsim::runtime::simir;

    const auto parsed = fsim::frontend::parse_text(
        "sv-element-process-template-replay.sv",
        R"(
module element_process_leaf #(
  parameter int LEFT = 3,
  parameter int RIGHT = 2
) (
  input logic clock,
  input logic [7:0] value,
  output logic [3:0] combined,
  output logic [3:0] sampled
);
  wire [3:0] elements[LEFT:RIGHT];
  assign elements[LEFT] = value[7:4];
  assign elements[RIGHT] = value[3:0];
  always_comb combined = elements[LEFT] ^ elements[RIGHT];
  always_ff @(posedge clock) sampled <= elements[LEFT];
endmodule

module element_process_top;
  logic clock;
  logic [7:0] left_value, right_value, ascending_value;
  wire [3:0] left_combined, right_combined, ascending_combined;
  wire [3:0] left_sampled, right_sampled, ascending_sampled;
  element_process_leaf left (
    .clock(clock), .value(left_value),
    .combined(left_combined), .sampled(left_sampled));
  element_process_leaf right (
    .clock(clock), .value(right_value),
    .combined(right_combined), .sampled(right_sampled));
  element_process_leaf #(.LEFT(2), .RIGHT(3)) ascending (
    .clock(clock), .value(ascending_value),
    .combined(ascending_combined), .sampled(ascending_sampled));
endmodule
)", fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());
    std::ostringstream profile_output;
    fsim::elaboration::ElaborationResult elaborated;
    {
        const ProfileCapture profile { profile_output };
        elaborated = compile_and_elaborate(
            parsed.design, "sv:work.element_process_top");
    }
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok() && elaborated.design);
    const auto output = profile_output.str();
    const auto row_start = output.find(
        "fsim-profile: systemverilog-ordinary-process-template ");
    assert(row_start != std::string::npos);
    const auto row_end = output.find('\n', row_start);
    const auto row = std::string_view { output }.substr(
        row_start, row_end == std::string::npos
            ? output.size() - row_start : row_end - row_start);
    assert(profile_metric(row, "occurrences") == 6U);
    assert(profile_metric(row, "lowerer_calls") == 4U);
    assert(profile_metric(row, "cached_programs") == 4U);
    assert(profile_metric(row, "replays") == 2U);

    const auto signal = [&](const std::string_view suffix) {
        const auto id = elaborated.design->find_signal(
            "element_process_top." + std::string { suffix });
        assert(id);
        return *id;
    };
    const auto writer = [&](const SignalId destination) -> const Process* {
        for (const auto& process : elaborated.design->processes()) {
            if (std::ranges::any_of(process.driver_regions,
                    [&](const auto& driver) {
                        return driver.signal == destination;
                    })) {
                return &process;
            }
        }
        return nullptr;
    };
    const auto* left_comb = writer(signal("left.$actual_combined"));
    const auto* right_comb = writer(signal("right.$actual_combined"));
    const auto* left_ff = writer(signal("left.$actual_sampled"));
    const auto* right_ff = writer(signal("right.$actual_sampled"));
    assert(signal("left.combined") == signal("left.$actual_combined"));
    assert(signal("right.combined") == signal("right.$actual_combined"));
    assert(signal("left.sampled") == signal("left.$actual_sampled"));
    assert(signal("right.sampled") == signal("right.$actual_sampled"));
    assert(left_comb && right_comb && left_ff && right_ff);
    assert(left_comb->operations.shares_body_with(right_comb->operations));
    assert(left_ff->operations.shares_body_with(right_ff->operations));
    assert(!right_comb->operations.instance_operation_overrides().empty());
    assert(!right_ff->operations.instance_operation_overrides().empty());
    const auto verify_element_read = [&](const Process& process,
                                        const SignalId element) {
        bool found { };
        for (std::size_t index = 0; index < process.operations.size(); ++index) {
            const auto operation = process.operations.expanded(index);
            if (const auto* read = operation_get_if<ReadSignal>(&operation)) {
                found = found || read->signal == element;
            }
        }
        assert(found);
    };
    const auto left_element = signal("left.elements[3]");
    const auto right_element = signal("right.elements[3]");
    assert(left_element != right_element);
    verify_element_read(*left_comb, left_element);
    verify_element_read(*right_comb, right_element);
    verify_element_read(*left_ff, left_element);
    verify_element_read(*right_ff, right_element);
    // The operation reads the physical leaf, while the static dependency is
    // represented by its owning array proxy.
    const auto right_proxy = signal("right.elements");
    const auto left_proxy = signal("left.elements");
    assert(std::ranges::any_of(right_comb->static_sensitivity,
        [&](const auto& sensitivity) {
            return sensitivity.signal == right_proxy;
        }));
    assert(std::ranges::none_of(right_comb->static_sensitivity,
        [&](const auto& sensitivity) {
            return sensitivity.signal == left_proxy;
        }));
    bool found_nba { };
    for (std::size_t index = 0; index < right_ff->operations.size(); ++index) {
        const auto operation = right_ff->operations.expanded(index);
        if (const auto* update = operation_get_if<WriteUpdate>(&operation)) {
            assert(update->domain == SignalUpdateDomain::systemverilog_nba);
            found_nba = true;
        }
    }
    assert(found_nba);

    fsim::diagnostic::Engine diagnostics;
    const auto encoded = fsim::app::runtime_path_codec::serialize_runtime_path_state(
        elaborated.design->state(), nullptr, diagnostics);
    assert(encoded);
    const auto decoded = fsim::app::runtime_path_codec::deserialize_runtime_path_state(
        *encoded, "element-process-template-replay", nullptr, diagnostics);
    assert(decoded);
    verify_element_read(decoded->processes.at(right_comb->id), right_element);
    verify_element_read(decoded->processes.at(right_ff->id), right_element);
    assert(decoded->processes.at(left_comb->id).operations.shares_body_with(
        decoded->processes.at(right_comb->id).operations));
    const auto encoded_again = fsim::app::runtime_path_codec::serialize_runtime_path_state(
        *decoded, nullptr, diagnostics);
    assert(encoded_again && *encoded_again == *encoded);

    auto interpreter = elaborated.design->create_interpreter();
    const auto deposit = [&](const std::string_view name,
                             const std::string_view value) {
        interpreter->deposit_signal(signal(name), PackedLogic4::from_msb_string(value));
    };
    const auto read = [&](const std::string_view name) {
        return interpreter->signal_value(signal(name)).to_msb_string();
    };
    const auto run = [&] {
        assert(interpreter->run().status == fsim::runtime::RunStatus::completed);
    };
    deposit("clock", "0");
    deposit("left_value", "10100101");
    deposit("right_value", "11000011");
    deposit("ascending_value", "00100111");
    run();
    assert(read("left_combined") == "1111");
    assert(read("right_combined") == "1111");
    assert(read("ascending_combined") == "0101");
    assert(read("left_sampled") == "XXXX");
    assert(read("left.sampled") == "XXXX");
    assert(read("left.$actual_sampled") == "XXXX");
    assert(read("right_sampled") == "XXXX");
    assert(read("ascending_sampled") == "XXXX");
    deposit("clock", "1");
    run();
    assert(read("left_sampled") == "1010");
    assert(read("right_sampled") == "1100");
    assert(read("ascending_sampled") == "0010");
    deposit("right_value", "1x0z0011");
    run();
    assert(read("left_combined") == "1111");
    assert(read("right_combined") == "1X1X");
    assert(read("right_sampled") == "1100");
    deposit("clock", "0");
    run();
    deposit("clock", "1");
    run();
    assert(read("right_sampled") == "1X0Z");
    assert(read("left_sampled") == "1010");
    assert(read("ascending_sampled") == "0010");
}

} // namespace fsim::tests::elaboration
