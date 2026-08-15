// SPDX-License-Identifier: Apache-2.0
#include "application_trace_control.hpp"
#include "application_trace_observation.hpp"
#include "fsim/app/application.hpp"
#include "fsim/runtime/fst_reader.hpp"
#include "fsim/runtime/fst_value_encoder.hpp"
#include "fsim/runtime/fst_writer.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using fsim::app::application_detail::TraceObservationKind;
using fsim::app::application_detail::TraceObservationLimits;
using fsim::app::application_detail::TraceObservationRecorder;
using fsim::app::application_detail::TraceObservationValue;
using fsim::app::application_detail::TraceSelectionControl;
using fsim::app::application_detail::TraceSelectionDeclaration;
using fsim::app::application_detail::TraceSelectionLimits;

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

template <typename Exception, typename Function>
void expect_throws(Function&& function)
{
    bool rejected = false;
    try {
        std::forward<Function>(function)();
    } catch (const Exception&) {
        rejected = true;
    }
    assert(rejected);
}

struct Fixture {
    fsim::runtime::TraceDeclarationModel declarations;
    std::array<fsim::runtime::TraceSignalId, 3> signals;
};

[[nodiscard]] Fixture make_fixture()
{
    fsim::runtime::TraceDeclarationBuilder builder;
    std::array<fsim::runtime::TraceSignalId, 3> signals {
        builder.add_variable("top.a", 1U),
        builder.add_variable("top.b", 4U),
        builder.add_variable("top.c", 2U),
    };
    return { std::move(builder).freeze(), signals };
}

[[nodiscard]] std::array<TraceSelectionDeclaration, 3>
selection_declarations(const Fixture& fixture)
{
    return { TraceSelectionDeclaration { 2U, fixture.signals[2], "top.c", false },
        TraceSelectionDeclaration { 0U, fixture.signals[0], "top.a", true },
        TraceSelectionDeclaration { 1U, fixture.signals[1], "top.b", false } };
}

[[nodiscard]] std::uint64_t read_be64(
    const std::string& bytes,
    const std::size_t offset)
{
    assert(offset + 8U <= bytes.size());
    std::uint64_t result = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        result = (result << 8U)
            | static_cast<unsigned char>(bytes[offset + index]);
    }
    return result;
}

[[nodiscard]] std::string run_debug_trace(
    const std::filesystem::path& directory,
    const fsim::project::TraceFormat format,
    const std::string_view identity,
    const bool select_a)
{
    const auto source = directory / "trace_control.sv";
    if (!std::filesystem::exists(source)) {
        std::ofstream(source) << R"(
module top;
  logic a;
  logic b;
  initial begin
    a = 1'b0;
    b = 1'b0;
    #1 a = 1'b1;
    b = 1'b1;
    #1 a = 1'b0;
    b = 1'b0;
  end
endmodule
)";
    }
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = std::string { identity };
    config.project.top = "sv:work.top";
    config.project.time_resolution = "1ns";
    config.build.cache_path = directory / (std::string { identity } + "-cache");
    config.run.max_deltas = 1'000U;
    const auto extension = format == fsim::project::TraceFormat::fst
        ? ".fst"
        : ".vcd";
    config.run.trace_file = directory / (std::string { identity } + extension);
    config.run.trace_format = format;
    config.run.trace_filters = { "__none__" };
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files = { source };
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(project);
    fsim::app::Simulation simulation(
        std::move(*project), config.run.max_deltas,
        fsim::app::SimulationEngine::debug);
    std::ostringstream output;
    std::ostringstream error;
    {
        fsim::app::DebuggerControl debugger(
            simulation, output, error, config, diagnostics);
        debugger.execute({ "trace", "status" });
        const auto first_status = output.str();
        output.str({ });
        output.clear();
        debugger.execute({ "trace", "status" });
        assert(output.str() == first_status);
        output.str({ });
        output.clear();
        if (select_a) {
            debugger.execute({ "trace", "add", "top.a" });
        }
        debugger.execute({ "run", "1ns" });
        if (select_a) {
            debugger.execute({ "trace", "remove", "top.a" });
        }
        debugger.execute({ "continue" });
    }
    if (!error.str().empty() || diagnostics.has_error()) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
        std::cerr << error.str();
    }
    assert(error.str().empty());
    assert(!diagnostics.has_error());
    assert(simulation.finished());
    assert(simulation.now() == 2U);

    std::ifstream input(*config.run.trace_file, std::ios::binary);
    assert(input);
    return { std::istreambuf_iterator<char> { input }, { } };
}

[[nodiscard]] std::string vcd_identifier(
    const std::string& trace,
    const std::string_view reference)
{
    std::istringstream lines { trace };
    for (std::string line; std::getline(lines, line);) {
        if (!line.starts_with("$var ")
            || !line.ends_with(" " + std::string { reference } + " $end")) {
            continue;
        }
        std::istringstream fields { line };
        std::string directive;
        std::string kind;
        std::string width;
        std::string identifier;
        fields >> directive >> kind >> width >> identifier;
        return identifier;
    }
    return { };
}

[[nodiscard]] std::size_t vcd_value_count(
    const std::string& trace,
    const std::string_view identifier)
{
    std::size_t count = 0U;
    std::istringstream lines { trace };
    for (std::string line; std::getline(lines, line);) {
        const auto scalar = line.size() == identifier.size() + 1U
            && line.substr(1U) == identifier;
        const auto vector = line.starts_with('b')
            && line.ends_with(" " + std::string { identifier });
        if (scalar || vector) {
            ++count;
        }
    }
    return count;
}

void test_selective_lifecycle_and_append_only_snapshots()
{
    using namespace fsim::runtime;
    const auto fixture = make_fixture();
    const auto declarations = selection_declarations(fixture);
    TraceSelectionControl selection(fixture.declarations, declarations);
    TraceObservationRecorder observations(fixture.declarations);

    std::ostringstream vcd_text;
    VcdWriter vcd(vcd_text);
    const auto handles = vcd.declare_model(fixture.declarations);
    vcd.begin();
    std::ostringstream fst_bytes(std::ios::binary);
    FstWriter fst(fst_bytes);
    fst.declare(fixture.declarations);
    fst.begin();
    static_cast<void>(observations.add_observer([&](const auto& record) {
        for (const auto& observed : record.values) {
            const TraceEvent event { observed.signal, record.time,
                record.delta, record.region, record.sequence };
            vcd.set_event(event);
            vcd.change(
                handles.at(observed.signal.value - 1U), observed.value);
            fst.change(event, encode_fst_logic_value(observed.value));
        }
    }));

    const auto initial_status = selection.status();
    assert(initial_status.declared == 3U);
    assert(initial_status.selected == 1U);
    assert(initial_status.generation == 0U);
    assert(initial_status.selected_owners == std::vector<std::string> { "top.a" });
    assert(selection.status() == initial_status);
    assert(selection.selected(0U));
    assert(!selection.selected(1U));
    assert(!selection.selected(2U));
    constexpr std::array<fsim::runtime::simir::SignalId, 3>
        expected_signals { 0U, 1U, 2U };
    assert(std::ranges::equal(
        selection.declared_signals(), expected_signals));

    const std::array initial { TraceObservationValue { fixture.signals[0],
        PackedLogic4::from_msb_string("0"), 0U } };
    static_cast<void>(observations.accept(TraceObservationKind::Signal,
        1U, 0U, TraceRegion::Active, "signal:0", initial));
    const auto before_unselected_change = observations.records().size();
    if (selection.selected(1U)) {
        const std::array unselected { TraceObservationValue {
            fixture.signals[1], PackedLogic4::from_msb_string("1111"), 1U } };
        static_cast<void>(observations.accept(TraceObservationKind::Signal,
            1U, 0U, TraceRegion::Inactive, "signal:1", unselected));
    }
    assert(observations.records().size() == before_unselected_change);

    assert(selection.set_enabled(1U, true, 1U, 0U,
        PackedLogic4::from_msb_string("1010"), observations));
    assert(selection.selected(1U));
    assert(observations.records().size() == 2U);
    assert(observations.records().back().region == TraceRegion::Callback);
    assert(observations.records().back().identity == "late-snapshot:1");
    assert(observations.records().back().values.front().value
        == PackedLogic4::from_msb_string("1010"));
    assert(selection.observation_region(
               1U, 0U, TraceRegion::Active)
        == TraceRegion::Callback);
    assert(selection.observation_region(
               1U, 1U, TraceRegion::Active)
        == TraceRegion::Active);
    assert(!selection.set_enabled(1U, true, 1U, 0U,
        PackedLogic4::from_msb_string("0000"), observations));
    assert(observations.records().size() == 2U);

    assert(selection.set_enabled(1U, false, 1U, 0U,
        PackedLogic4::from_msb_string("0000"), observations));
    assert(!selection.selected(1U));
    assert(observations.records().size() == 2U);
    const auto disabled_status = selection.status();
    assert(disabled_status.selected_owners
        == std::vector<std::string> { "top.a" });
    assert(selection.status() == disabled_status);

    assert(selection.set_enabled(1U, true, 1U, 0U,
        PackedLogic4::from_msb_string("0011"), observations));
    assert(observations.records().size() == 3U);
    assert(fsim::runtime::trace_event_precedes(
        { fixture.signals[1], observations.records()[1].time,
            observations.records()[1].delta, observations.records()[1].region,
            observations.records()[1].sequence },
        { fixture.signals[1], observations.records()[2].time,
            observations.records()[2].delta, observations.records()[2].region,
            observations.records()[2].sequence }));

    const std::array later { TraceObservationValue { fixture.signals[0],
        PackedLogic4::from_msb_string("1"), 0U } };
    static_cast<void>(observations.accept(TraceObservationKind::Signal,
        2U, 0U, TraceRegion::Active, "signal:0", later));
    assert(selection.set_enabled(1U, false, 2U, 0U,
        PackedLogic4::from_msb_string("0011"), observations));
    const auto before_reordered = observations.records().size();
    expect_throws<std::logic_error>([&] {
        static_cast<void>(selection.set_enabled(2U, true, 1U, 0U,
            PackedLogic4::from_msb_string("10"), observations));
    });
    assert(!selection.selected(2U));
    assert(observations.records().size() == before_reordered);

    vcd.flush();
    fst.close(2U);
    assert(vcd_text.str().find("#1") != std::string::npos);
    assert(vcd_text.str().find("b1010") != std::string::npos);
    assert(vcd_text.str().find("b0011") != std::string::npos);
    const auto read = read_fst(fst_bytes.str());
    assert(read.ok());
    assert(read.trace->timestamps
        == std::vector<SimulationTick>({ 1U, 2U }));
    assert(read.trace->values.size() == 4U);
    assert(read.trace->values[0].signal == fixture.signals[0]);
    assert(read.trace->values[0].time == 1U);
    assert(read.trace->values[0].payload == "0");
    assert(read.trace->values[1].signal == fixture.signals[0]);
    assert(read.trace->values[1].time == 2U);
    assert(read.trace->values[1].payload == "1");
    assert(read.trace->values[2].signal == fixture.signals[1]);
    assert(read.trace->values[2].time == 1U);
    assert(read.trace->values[2].payload == "1010");
    assert(read.trace->values[3].signal == fixture.signals[1]);
    assert(read.trace->values[3].time == 1U);
    assert(read.trace->values[3].payload == "0011");
}

void test_snapshot_checks_every_existing_record()
{
    using namespace fsim::runtime;
    const auto fixture = make_fixture();
    const auto declarations = selection_declarations(fixture);
    TraceSelectionControl selection(fixture.declarations, declarations);
    TraceObservationRecorder observations(fixture.declarations);
    const std::array first { TraceObservationValue { fixture.signals[0],
        PackedLogic4::from_msb_string("0"), 0U } };
    static_cast<void>(observations.accept(TraceObservationKind::Signal,
        5U, 0U, TraceRegion::Active, "future", first));
    static_cast<void>(observations.accept(TraceObservationKind::Signal,
        1U, 0U, TraceRegion::Active, "appended-late", first));
    expect_throws<std::logic_error>([&] {
        static_cast<void>(selection.set_enabled(1U, true, 2U, 0U,
            PackedLogic4::from_msb_string("1010"), observations));
    });
    assert(!selection.selected(1U));
    assert(observations.records().size() == 2U);
}

void test_transactional_and_resource_negatives()
{
    using namespace fsim::runtime;
    const auto fixture = make_fixture();
    const auto declarations = selection_declarations(fixture);

    expect_throws<std::invalid_argument>([&] {
        auto invalid = declarations;
        invalid[1].owner = "top.b";
        static_cast<void>(TraceSelectionControl(
            fixture.declarations, invalid));
    });
    expect_throws<std::invalid_argument>([&] {
        auto invalid = declarations;
        invalid[1].runtime_signal = invalid[0].runtime_signal;
        static_cast<void>(TraceSelectionControl(
            fixture.declarations, invalid));
    });
    expect_throws<std::invalid_argument>([&] {
        auto invalid = declarations;
        invalid[1].trace_signal = TraceSignalId { 999U };
        static_cast<void>(TraceSelectionControl(
            fixture.declarations, invalid));
    });
    expect_throws<std::invalid_argument>([&] {
        auto invalid = declarations;
        invalid[1].owner = std::string { "bad\0owner", 9U };
        static_cast<void>(TraceSelectionControl(
            fixture.declarations, invalid));
    });
    expect_throws<std::length_error>([&] {
        TraceSelectionLimits limits;
        limits.maximum_signals = 2U;
        static_cast<void>(TraceSelectionControl(
            fixture.declarations, declarations, limits));
    });
    expect_throws<std::invalid_argument>([&] {
        TraceSelectionLimits limits;
        limits.maximum_owner_bytes = 0U;
        static_cast<void>(TraceSelectionControl(
            fixture.declarations, declarations, limits));
    });

    TraceSelectionControl selection(fixture.declarations, declarations);
    TraceObservationLimits observation_limits;
    observation_limits.maximum_records = 1U;
    TraceObservationRecorder observations(
        fixture.declarations, observation_limits);
    const std::array initial { TraceObservationValue { fixture.signals[0],
        PackedLogic4::from_msb_string("0"), 0U } };
    static_cast<void>(observations.accept(TraceObservationKind::Signal,
        1U, 0U, TraceRegion::Active, "signal:0", initial));
    const auto status = selection.status();
    expect_throws<std::length_error>([&] {
        static_cast<void>(selection.set_enabled(1U, true, 2U, 0U,
            PackedLogic4::from_msb_string("1010"), observations));
    });
    assert(selection.status() == status);
    assert(!selection.selected(1U));
    expect_throws<std::logic_error>([&] {
        static_cast<void>(selection.set_enabled(99U, true, 2U, 0U,
            PackedLogic4::from_msb_string("1"), observations));
    });
}

void test_debugger_paths_for_vcd_and_fst()
{
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    TemporaryDirectory directory { std::filesystem::temp_directory_path()
        / ("fsim-trace-control-" + std::to_string(nonce)) };
    std::filesystem::create_directories(directory.path);

    const auto vcd = run_debug_trace(
        directory.path, fsim::project::TraceFormat::vcd, "selected-vcd", true);
    const auto a_identifier = vcd_identifier(vcd, "a");
    const auto b_identifier = vcd_identifier(vcd, "b");
    assert(!a_identifier.empty());
    assert(!b_identifier.empty());
    assert(vcd_value_count(vcd, a_identifier) >= 2U);
    assert(vcd_value_count(vcd, b_identifier) == 0U);

    const auto fst_first = run_debug_trace(
        directory.path, fsim::project::TraceFormat::fst, "selected-fst-a", true);
    const auto fst_second = run_debug_trace(
        directory.path, fsim::project::TraceFormat::fst, "selected-fst-b", true);
    const auto fst_unselected = run_debug_trace(
        directory.path, fsim::project::TraceFormat::fst, "unselected-fst", false);
    assert(fst_first == fst_second);
    assert(fst_first != fst_unselected);
    assert(fst_first.size() > 363U);
    assert(static_cast<unsigned char>(fst_first.front()) == 0U);
    assert(read_be64(fst_first, 1U) == 329U);
    assert(read_be64(fst_first, 17U) == 2U);
    const auto selected = fsim::runtime::read_fst(fst_first);
    const auto repeated = fsim::runtime::read_fst(fst_second);
    const auto unselected = fsim::runtime::read_fst(fst_unselected);
    assert(selected.ok() && repeated.ok() && unselected.ok());
    assert(selected.trace->semantic_digest == repeated.trace->semantic_digest);
    assert(!selected.trace->values.empty());
    const auto selected_a = std::ranges::find(selected.trace->declarations,
        "top.a", &fsim::runtime::FstReaderDeclaration::path);
    const auto selected_b = std::ranges::find(selected.trace->declarations,
        "top.b", &fsim::runtime::FstReaderDeclaration::path);
    assert(selected_a != selected.trace->declarations.end());
    assert(selected_b != selected.trace->declarations.end());
    assert(std::ranges::any_of(selected.trace->values,
        [&](const auto& value) {
            return value.signal == selected_a->signal;
        }));
    assert(std::ranges::none_of(selected.trace->values,
        [&](const auto& value) {
            return value.signal == selected_b->signal;
        }));
    const auto unselected_a = std::ranges::find(unselected.trace->declarations,
        "top.a", &fsim::runtime::FstReaderDeclaration::path);
    const auto unselected_b = std::ranges::find(unselected.trace->declarations,
        "top.b", &fsim::runtime::FstReaderDeclaration::path);
    assert(unselected_a != unselected.trace->declarations.end());
    assert(unselected_b != unselected.trace->declarations.end());
    assert(std::ranges::none_of(unselected.trace->values,
        [&](const auto& value) {
            return value.signal == unselected_a->signal
                || value.signal == unselected_b->signal;
        }));
}

} // namespace

int main()
{
    test_selective_lifecycle_and_append_only_snapshots();
    test_snapshot_checks_every_existing_record();
    test_transactional_and_resource_negatives();
    test_debugger_paths_for_vcd_and_fst();
}
