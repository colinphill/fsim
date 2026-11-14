// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/built_project.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/artifact/coverage_database_codec.hpp"
#include "fsim/cli/driver.hpp"
#include "fsim/elaboration/coverage_inventory.hpp"
#include "fsim/frontend/coverage_point_identity.hpp"
#include "fsim/project/configuration.hpp"
#include "fsim/project/project.hpp"
#include "fsim/runtime/simir_interpreter.hpp"

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
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;

    TemporaryDirectory()
    {
        const auto nonce = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        path = std::filesystem::temp_directory_path()
            / ("fsim-code-coverage-control-" + std::to_string(nonce));
        std::filesystem::create_directories(path);
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

void write_text(
    const std::filesystem::path& path,
    const std::string_view text)
{
    std::ofstream output(path, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    assert(output.good());
}

constexpr std::string_view kCoverageSelectionSource = R"(
module selector_leaf;
  int covered;
  initial covered = 1;
endmodule
module selector_top;
  selector_leaf u0();
  selector_leaf u1();
  int hierarchy_status, instance_status;
  int leaf_maximum, instance_maximum, hierarchy_maximum;
  int missing_selector, invalid_scope;
  int invalid_type, invalid_get_type;
  initial begin
    hierarchy_status = $coverage_control(`SV_COV_CHECK, `SV_COV_STATEMENT, `SV_COV_HIER, $root);
    instance_status = $coverage_control(`SV_COV_CHECK, `SV_COV_STATEMENT, `SV_COV_MODULE, selector_top.u0);
    leaf_maximum = $coverage_get_max(`SV_COV_STATEMENT, `SV_COV_MODULE, "selector_leaf");
    instance_maximum = $coverage_get_max(`SV_COV_STATEMENT, `SV_COV_MODULE, selector_top.u0);
    hierarchy_maximum = $coverage_get_max(`SV_COV_STATEMENT, `SV_COV_HIER, selector_top);
    missing_selector = $coverage_control(`SV_COV_CHECK, `SV_COV_STATEMENT, `SV_COV_MODULE, "missing");
    invalid_scope = $coverage_control(`SV_COV_CHECK, `SV_COV_STATEMENT, 99, "selector_top");
    invalid_type = $coverage_control(`SV_COV_CHECK, 99, `SV_COV_MODULE, "selector_top");
    invalid_get_type = $coverage_get(99, `SV_COV_MODULE, "selector_top");
  end
endmodule
)";

// `elaborate.code_coverage` in fsim.tcl turns coverage on as
// --code-coverage does; an elaboration config keeps it off by default.
void test_configuration_surface()
{
    const fsim::project::Config disabled;
    assert(!disabled.coverage.enabled);
    assert(!fsim::app::code_coverage_enabled(disabled));
    fsim::project::Config enabled;
    enabled.coverage.enabled = true;
    assert(fsim::app::code_coverage_enabled(enabled));

    const fsim::project::ConfigOrigin origin {
        fsim::project::ConfigLayer::workspace, "fsim.tcl", 1U };
    fsim::project::Configuration configuration;
    assert(configuration.find("elaborate.code_coverage") == nullptr);
    assert(!configuration.set("elaborate.code_coverage", { "true" }, origin));
    const auto* setting = configuration.find("elaborate.code_coverage");
    assert(setting != nullptr && setting->values.size() == 1U
        && setting->values.front() == "true");
    // Tcl's boolean spellings normalize; anything else is refused.
    assert(!configuration.set("elaborate.code_coverage", { "off" }, origin));
    assert(configuration.find("elaborate.code_coverage")->values.front()
        == "false");
    assert(!configuration.set("elaborate.code_coverage", { "yes" }, origin));
    assert(configuration.set("elaborate.code_coverage", { "maybe" }, origin));
    assert(configuration.set("elaborate.code_coverage_unknown", { "true" },
        origin));
    assert(configuration.find("elaborate.code_coverage")->values.front()
        == "true");
}

void test_cli_override(const TemporaryDirectory& directory)
{
    bool invoked = false;
    fsim::cli::Services services;
    services.simulate = [&](const fsim::cli::Invocation& invocation,
                       const fsim::project::Config& config,
                       fsim::diagnostic::Engine&,
                       std::ostream&,
                       std::ostream&) {
        invoked = true;
        assert(invocation.code_coverage == std::optional<bool> { true });
        assert(config.coverage.enabled);
        assert(fsim::app::code_coverage_enabled(config));
        return 0;
    };
    const std::array arguments {
        "fsim", "simulate", "--code-coverage" };
    std::ostringstream output;
    std::ostringstream error;
    assert(fsim::cli::run(static_cast<int>(arguments.size()),
               arguments.data(), services, output, error)
        == 0);
    assert(invoked);
    assert(error.str().empty());

    const auto source_text = (directory.path / "top.sv").generic_string();
    const std::array invalid { "fsim", "check", "--code-coverage",
        source_text.c_str() };
    fsim::diagnostic::Engine invalid_diagnostics;
    assert(!fsim::cli::parse_arguments(static_cast<int>(invalid.size()),
        invalid.data(), invalid_diagnostics));
    assert(invalid_diagnostics.has_error());
}

void test_disabled_build_has_no_runtime_state(
    const TemporaryDirectory& directory)
{
    fsim::project::Config config;
    config.base_directory = directory.path;
    config.project.name = "coverage-control";
    config.project.top = "sv:work.top";
    config.project.tops = { { "sv:work.top", "top" } };
    config.build.cache_path = directory.path / "cache";
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.files = { directory.path / "top.sv" };
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    const auto built = fsim::app::build_project(config, diagnostics);
    assert(built && !diagnostics.has_error());
    assert(!built->code_coverage_enabled);
    assert(!built->design.code_coverage_inventory());

    config.coverage.enabled = true;
    fsim::diagnostic::Engine enabled_diagnostics;
    const auto enabled = fsim::app::build_project(config, enabled_diagnostics);
    assert(enabled && !enabled_diagnostics.has_error());
    assert(enabled->code_coverage_enabled);

    // A configured exclusion keeps the points it selects counted, reported
    // as excluded for its reason.
    config.coverage.exclusions.push_back({ .source = std::nullopt,
        .hierarchy = "top", .object = std::nullopt, .metric = "statement",
        .reason = "waived by plan" });
    fsim::diagnostic::Engine waived_diagnostics;
    const auto waived = fsim::app::build_project(config, waived_diagnostics);
    assert(waived && !waived_diagnostics.has_error());
    const auto& inventory = waived->design.code_coverage_inventory();
    assert(inventory);
    std::size_t statements { };
    for (const auto& instance : inventory->instances) {
        for (const auto& point : instance.points) {
            if (point.point.metric
                == fsim::runtime::CodeCoverageMetric::Statement) {
                assert(point.exclusion_reason == "waived by plan");
                ++statements;
            }
        }
    }
    assert(statements != 0U);
    config.coverage.exclusions.clear();

    config.coverage.exclusions.push_back({ .source = std::nullopt,
        .hierarchy = std::nullopt, .object = std::nullopt,
        .metric = "unknown", .reason = "must fail before elaboration" });
    fsim::diagnostic::Engine exclusion_diagnostics;
    assert(!fsim::app::build_project(config, exclusion_diagnostics));
    assert(std::ranges::any_of(exclusion_diagnostics.diagnostics(),
        [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-COV-042";
        }));
}

std::uint32_t read_u32(
    const fsim::app::Simulation& simulation,
    const std::string_view name)
{
    const auto signal = simulation.find_signal(
        "top." + std::string { name });
    assert(signal);
    const auto value = simulation.read_signal(*signal).low_word();
    assert(value.bval == 0U);
    return static_cast<std::uint32_t>(value.aval);
}

void test_systemverilog_coverage_control(
    const TemporaryDirectory& directory,
    const fsim::project::Optimization optimization,
    const fsim::app::SimulationEngine engine,
    const bool enabled)
{
    fsim::project::Config config;
    config.base_directory = directory.path;
    config.project.name = "coverage-control-runtime";
    config.project.top = "sv:work.top";
    config.project.tops = { { "sv:work.top", "top" } };
    config.build.optimization = optimization;
    config.build.cache_path = directory.path
        / (optimization == fsim::project::Optimization::o0
                ? "runtime-cache-o0"
                : "runtime-cache-o2");
    config.coverage.enabled = enabled;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2005";
    sources.files = { directory.path / "coverage_control.sv" };
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    auto built = fsim::app::build_project(config, diagnostics);
    if (!built) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(built && !diagnostics.has_error());
    const auto encoded = fsim::app::serialize_runtime_state(
        built->design, diagnostics);
    assert(encoded && !diagnostics.has_error());
    auto restored = fsim::app::deserialize_runtime_state(
        *encoded, "coverage-control-runtime", diagnostics);
    assert(restored && !diagnostics.has_error());

    fsim::app::Simulation simulation { std::move(*built), 1000U, engine };
    auto& vpi_objects = simulation.systemverilog_vpi_objects();
    auto& vpi_coverage = simulation.systemverilog_vpi_coverage();
    const auto top = vpi_objects.find("top");
    assert(vpi_coverage.valid() && top);
    const auto statement_property = vpi_coverage.property(
        fsim::runtime::SystemVerilogVpiCoverageProperty::StatementCoverage,
        top.value->handle);
    assert(statement_property
        && statement_property.value == (enabled ? 1 : 0));
    assert(vpi_coverage.target_count() == (enabled ? 1U : 0U));
    const auto result = simulation.run();
    assert(result.status == fsim::runtime::RunStatus::completed);
    const auto expected = enabled ? 1U : 0U;
    assert(read_u32(simulation, "start_status") == expected);
    assert(read_u32(simulation, "stop_status") == expected);
    assert(read_u32(simulation, "reset_status") == expected);
    assert(read_u32(simulation, "check_status") == expected);
    assert(read_u32(simulation, "unsupported_status") == 0U);
    assert(read_u32(simulation, "invalid_status") == UINT32_MAX);
    assert(read_u32(simulation, "unknown_status") == UINT32_MAX);
    const auto current = read_u32(simulation, "coverage_current");
    const auto maximum = read_u32(simulation, "coverage_maximum");
    assert(current == (enabled ? 5U : 0U));
    assert(maximum == (enabled ? 27U : 0U));
    assert(read_u32(simulation, "coverage_save") == expected);
    assert(read_u32(simulation, "unsupported_get") == 0U);
    assert(read_u32(simulation, "constant_start") == 0U);
    assert(read_u32(simulation, "constant_stop") == 1U);
    assert(read_u32(simulation, "constant_reset") == 2U);
    assert(read_u32(simulation, "constant_check") == 3U);
    assert(read_u32(simulation, "constant_module") == 10U);
    assert(read_u32(simulation, "constant_hier") == 11U);
    assert(read_u32(simulation, "constant_assertion") == 20U);
    assert(read_u32(simulation, "constant_fsm") == 21U);
    assert(read_u32(simulation, "constant_statement") == 22U);
    assert(read_u32(simulation, "constant_toggle") == 23U);
    assert(read_u32(simulation, "constant_overflow")
        == static_cast<std::uint32_t>(-2));
    assert(read_u32(simulation, "constant_error") == UINT32_MAX);
    assert(read_u32(simulation, "constant_nocov") == 0U);
    assert(read_u32(simulation, "constant_ok") == 1U);
    assert(read_u32(simulation, "constant_partial") == 2U);
    assert(simulation.compiled_process_count() == 0U);
}

void test_standard_coverage_selection(
    const TemporaryDirectory& directory,
    const fsim::app::SimulationEngine engine)
{
    fsim::project::Config config;
    config.base_directory = directory.path;
    config.project.name = "coverage-selection-runtime";
    config.project.top = "sv:work.selector_top";
    config.project.tops = { { "sv:work.selector_top", "selector_top" } };
    config.build.optimization = fsim::project::Optimization::o2;
    config.build.cache_path = directory.path / "selector-cache";
    // This selector fixture attaches a deliberately small manual inventory.
    config.coverage.enabled = false;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2005";
    sources.files = { directory.path / "coverage_selection.sv" };
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    auto built = fsim::app::build_project(config, diagnostics);
    if (!built) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(built && !diagnostics.has_error());
    const auto source_identity
        = fsim::frontend::make_code_coverage_source_identity(
            directory.path, directory.path / "coverage_selection.sv",
            std::as_bytes(std::span { kCoverageSelectionSource.data(),
                kCoverageSelectionSource.size() }));
    assert(source_identity.ok());
    assert(!built->design.specializations().empty());
    const std::array inventory_sources {
        fsim::elaboration::CoverageInventorySource {
            built->design.specializations().front().source,
            *source_identity.identity }
    };
    const auto point_identity
        = fsim::frontend::make_code_coverage_point_identity(
            inventory_sources.front().identity,
            fsim::frontend::CodeCoverageLanguage::SystemVerilog,
            fsim::frontend::CodeCoverageConstructKind::Statement,
            { 1U, 2U });
    assert(point_identity.ok());
    std::vector<fsim::elaboration::CoverageInstanceInventoryDraft> drafts;
    for (const auto& specialization : built->design.specializations()) {
        drafts.push_back({ specialization.id,
            { { *point_identity.identity,
                fsim::runtime::CodeCoverageMetric::Statement,
                0U, { 1U, 2U }, 1U, { } } }, { } });
    }
    assert(built->design
            .attach_code_coverage_inventory(inventory_sources, drafts)
            .ok());
    built->code_coverage_enabled = true;
    fsim::app::Simulation simulation {
        std::move(*built), 1000U, engine,
        fsim::app::SystemVerilogVpiRuntimeUpdates::omitted
    };
    assert(simulation.run().status == fsim::runtime::RunStatus::completed);
    const auto selected_read = [&](const std::string_view name) {
        const auto signal = simulation.find_signal(
            "selector_top." + std::string { name });
        assert(signal);
        const auto value = simulation.read_signal(*signal).low_word();
        assert(value.bval == 0U);
        return static_cast<std::uint32_t>(value.aval);
    };
    assert(selected_read("hierarchy_status") == 1U);
    assert(selected_read("instance_status") == 1U);
    const auto leaf_maximum = selected_read("leaf_maximum");
    const auto instance_maximum = selected_read("instance_maximum");
    const auto hierarchy_maximum = selected_read("hierarchy_maximum");
    assert(leaf_maximum == 2U);
    assert(instance_maximum == 1U);
    assert(hierarchy_maximum == 3U);
    assert(selected_read("missing_selector") == UINT32_MAX);
    assert(selected_read("invalid_scope") == UINT32_MAX);
    assert(selected_read("invalid_type") == UINT32_MAX);
    assert(selected_read("invalid_get_type") == UINT32_MAX);
}

void test_counter_control_semantics()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;
    Interpreter interpreter;
    const auto status = interpreter.add_signal(
        { "top.status", PackedLogic4(32U, Logic4::zero) });
    Process process;
    process.id = 0U;
    process.name = "coverage-control-counters";
    process.register_count = 4U;
    process.string_register_count = 1U;
    const auto constant = [](const std::uint32_t value) {
        return PackedLogic4::from_aval_bval(32U, value, 0U);
    };
    process.operations = {
        LoadConstant { 0U, constant(1U) },
        LoadConstant { 1U, constant(22U) },
        LoadConstant { 3U, constant(10U) },
        LoadStringConstant { 0U, "top" },
        CoverageControl { 2U, 0U, 1U, 3U, 0U, "top", true },
        CodeCoverageHit { { 1U, 1U }, CodeCoverageMetric::Statement,
            { 0U } },
        CodeCoverageHit { { 1U, 2U }, CodeCoverageMetric::Statement,
            { 1U } },
        LoadConstant { 0U, constant(0U) },
        CoverageControl { 2U, 0U, 1U, 3U, 0U, "top", true },
        CodeCoverageHit { { 1U, 1U }, CodeCoverageMetric::Statement,
            { 0U } },
        LoadConstant { 0U, constant(2U) },
        CoverageControl { 2U, 0U, 1U, 3U, 0U, "top", true },
        CodeCoverageHit { { 1U, 1U }, CodeCoverageMetric::Statement,
            { 0U } },
        LoadConstant { 0U, constant(3U) },
        CoverageControl { 2U, 0U, 1U, 3U, 0U, "top", true },
        WriteBlocking { status, 2U },
        Halt { },
    };
    interpreter.set_code_coverage_counters({ 0U, 0U });
    interpreter.set_coverage_control_hook([&](const auto& event) {
        const std::array counters { CodeCoverageCounterId { 0U } };
        switch (static_cast<SystemVerilogCoverageCommand>(event.command)) {
        case SystemVerilogCoverageCommand::start:
            assert(interpreter.set_code_coverage_collection_enabled(
                counters, true));
            break;
        case SystemVerilogCoverageCommand::stop:
            assert(interpreter.set_code_coverage_collection_enabled(
                counters, false));
            break;
        case SystemVerilogCoverageCommand::reset:
            assert(interpreter.reset_code_coverage_counters(counters));
            break;
        case SystemVerilogCoverageCommand::check:
            break;
        }
        assert(event.scope == 10 && event.selector == "top"
            && event.selector_is_instance);
        return static_cast<std::int32_t>(
            SystemVerilogCoverageStatus::ok);
    });
    (void)interpreter.add_process(std::move(process));
    assert(interpreter.run().status == RunStatus::completed);
    assert(interpreter.code_coverage_collection_enabled());
    assert(interpreter.code_coverage_counters().size() == 2U
        && interpreter.code_coverage_counters()[0] == 1U
        && interpreter.code_coverage_counters()[1] == 1U);
    assert(interpreter.signal_value(status).low_word().aval == 1U);
}

void test_coverage_access_semantics()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;
    const auto constant = [](const std::uint32_t value) {
        return PackedLogic4::from_aval_bval(32U, value, 0U);
    };
    Interpreter interpreter;
    const auto maximum = interpreter.add_signal(
        { "top.maximum", PackedLogic4(32U, Logic4::zero) });
    const auto current = interpreter.add_signal(
        { "top.current", PackedLogic4(32U, Logic4::zero) });
    Process process;
    process.id = 0U;
    process.name = "coverage-access-counters";
    process.register_count = 3U;
    process.string_register_count = 1U;
    process.operations = {
        LoadConstant { 0U, constant(22U) },
        LoadConstant { 2U, constant(10U) },
        LoadStringConstant { 0U, "top" },
        CoverageAccess { 1U, SystemVerilogCoverageAccessKind::get_max,
            0U, RegisterId { 2U }, StringRegisterId { 0U }, "top", true,
            std::nullopt },
        CodeCoverageHit { { 2U, 2U }, CodeCoverageMetric::Statement,
            { 0U } },
        CoverageAccess { 2U, SystemVerilogCoverageAccessKind::get,
            0U, RegisterId { 2U }, StringRegisterId { 0U }, "top", true,
            std::nullopt },
        WriteBlocking { maximum, 1U },
        WriteBlocking { current, 2U },
        Halt { },
    };
    interpreter.set_code_coverage_counters({ 0U, 0U });
    (void)interpreter.add_process(std::move(process));
    assert(interpreter.run().status == RunStatus::completed);
    assert(interpreter.signal_value(maximum).low_word().aval == 2U);
    assert(interpreter.signal_value(current).low_word().aval == 1U);

    Interpreter service;
    const auto save = service.add_signal(
        { "top.save", PackedLogic4(32U, Logic4::zero) });
    const auto merge = service.add_signal(
        { "top.merge", PackedLogic4(32U, Logic4::zero) });
    std::vector<CoverageAccessEvent> events;
    service.set_coverage_access_hook([&](const auto& event) {
        events.push_back(event);
        return static_cast<std::int32_t>(SystemVerilogCoverageStatus::ok);
    });
    Process file_process;
    file_process.id = 0U;
    file_process.name = "coverage-access-files";
    file_process.register_count = 2U;
    file_process.string_register_count = 1U;
    file_process.operations = {
        LoadConstant { 0U, constant(22U) },
        LoadStringConstant { 0U, "coverage.fsimcov" },
        CoverageAccess { 1U, SystemVerilogCoverageAccessKind::save,
            0U, std::nullopt, std::nullopt, "", false,
            StringRegisterId { 0U } },
        WriteBlocking { save, 1U },
        CoverageAccess { 1U, SystemVerilogCoverageAccessKind::merge,
            0U, std::nullopt, std::nullopt, "", false,
            StringRegisterId { 0U } },
        WriteBlocking { merge, 1U },
        Halt { },
    };
    (void)service.add_process(std::move(file_process));
    assert(service.run().status == RunStatus::completed);
    assert(events.size() == 2U
        && events[0].kind == SystemVerilogCoverageAccessKind::save
        && events[1].kind == SystemVerilogCoverageAccessKind::merge
        && events[0].coverage_type == 22
        && events[0].filename == "coverage.fsimcov"
        && events[1].filename == "coverage.fsimcov");
    assert(service.signal_value(save).low_word().aval == 1U);
    assert(service.signal_value(merge).low_word().aval == 1U);
}

void test_coverage_control_negative_profiles(
    const TemporaryDirectory& directory)
{
    write_text(directory.path / "invalid_coverage_control.sv",
        "module top; int result; initial result = $coverage_control(0); endmodule\n");
    fsim::project::Config config;
    config.base_directory = directory.path;
    config.project.top = "sv:work.top";
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2005";
    sources.files = { directory.path / "invalid_coverage_control.sv" };
    config.source_sets.push_back(std::move(sources));
    fsim::diagnostic::Engine diagnostics;
    assert(!fsim::app::build_project(config, diagnostics));
    assert(std::ranges::any_of(
        diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-COV-038";
        }));

    write_text(directory.path / "invalid_coverage_control.v",
        "module top; integer result; initial result = `SV_COV_OK; endmodule\n");
    config.source_sets.front().language = fsim::project::Language::verilog;
    config.source_sets.front().standard = "2005";
    config.source_sets.front().files
        = { directory.path / "invalid_coverage_control.v" };
    fsim::diagnostic::Engine verilog_diagnostics;
    assert(!fsim::app::build_project(config, verilog_diagnostics));
    assert(verilog_diagnostics.has_error());

    write_text(directory.path / "invalid_coverage_access.sv",
        "module top; int result; initial result = $coverage_save(`SV_COV_STATEMENT, 1); endmodule\n");
    config.source_sets.front().language
        = fsim::project::Language::system_verilog;
    config.source_sets.front().standard = "2005";
    config.source_sets.front().files
        = { directory.path / "invalid_coverage_access.sv" };
    fsim::diagnostic::Engine access_diagnostics;
    assert(!fsim::app::build_project(config, access_diagnostics));
    assert(std::ranges::any_of(
        access_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-COV-039";
        }));
}

} // namespace

// FSM points of one saved database, as (family, sorted hits).
std::vector<std::pair<int, std::vector<std::uint64_t>>> fsm_hits(
    const std::filesystem::path& path)
{
    const auto database = fsim::artifact::read_coverage_database(path);
    assert(database.ok());
    std::vector<std::pair<int, std::vector<std::uint64_t>>> result {
        { static_cast<int>(
              fsim::artifact::CoverageDatabaseMetricFamily::FsmState),
            { } },
        { static_cast<int>(
              fsim::artifact::CoverageDatabaseMetricFamily::FsmTransition),
            { } }
    };
    for (const auto& metric : database.contents->metrics) {
        if (metric.scope
            != fsim::artifact::CoverageDatabaseMetricScope::Instance) {
            continue;
        }
        for (auto& [family, hits] : result) {
            if (static_cast<int>(metric.family) == family) {
                hits.push_back(metric.hits);
            }
        }
    }
    for (auto& [family, hits] : result) {
        (void)family;
        std::ranges::sort(hits);
    }
    return result;
}

// A configured FSM hint (fsim::coverage fsm add) is the only evidence that
// a vector with named-constant case choices is a state machine; a hint for
// another instance leaves it uncovered. Both engines count the same.
void test_configured_fsm_hint(const TemporaryDirectory& directory,
    const bool vhdl, const std::string_view instance)
{
    std::vector<std::pair<int, std::vector<std::uint64_t>>> reference;
    for (const auto engine : { fsim::app::SimulationEngine::interpreter,
             fsim::app::SimulationEngine::v4 }) {
        fsim::project::Config config;
        config.base_directory = directory.path;
        config.project.name = "coverage-manifest-fsm";
        config.project.top = vhdl ? "vhdl:work.mtb(sim)" : "sv:work.mtb";
        config.project.time_resolution = "1ns";
        config.build.cache_path = directory.path
            / (vhdl ? "manifest-fsm-vhdl-cache" : "manifest-fsm-sv-cache");
        config.coverage.enabled = true;
        config.coverage.fsm_hints.push_back({ std::string { instance },
            "state", std::nullopt,
            std::vector<std::string> { vhdl ? "idle" : "IDLE",
                vhdl ? "run" : "RUN", vhdl ? "done" : "DONE" } });
        fsim::project::SourceSet sources;
        sources.language = vhdl ? fsim::project::Language::vhdl
                                : fsim::project::Language::system_verilog;
        sources.standard = vhdl ? "2008" : "2017";
        sources.library = "work";
        sources.files = { directory.path
            / (vhdl ? "manifest_fsm.vhd" : "manifest_fsm.sv") };
        config.source_sets.push_back(std::move(sources));

        fsim::diagnostic::Engine diagnostics;
        auto built = fsim::app::build_project(config, diagnostics);
        if (!built) {
            for (const auto& diagnostic : diagnostics.diagnostics()) {
                std::cerr << diagnostic.code << ": " << diagnostic.message
                          << '\n';
            }
        }
        assert(built && !diagnostics.has_error());
        fsim::app::Simulation simulation { std::move(*built), 1000U, engine };
        const auto result = simulation.run();
        assert(result.status == fsim::runtime::RunStatus::completed
            || result.status == fsim::runtime::RunStatus::stopped);
        const auto path = directory.path / "manifest-fsm.fsimcov";
        assert(!simulation.save_code_coverage_database(path));
        const auto hits = fsm_hits(path);
        if (reference.empty()) {
            reference = hits;
        }
        assert(hits == reference);
    }
    if (instance == "mtb.u") {
        // IDLE -> RUN -> DONE -> IDLE -> RUN over four edges, entered from
        // the initial IDLE.
        assert(reference[0].second.size() == 3U
            && std::ranges::all_of(reference[0].second,
                [](const auto hits) { return hits != 0U; }));
        assert(reference[1].second.size() >= 3U
            && std::ranges::count_if(reference[1].second,
                   [](const auto hits) { return hits != 0U; })
                == 3);
    } else {
        assert(reference[0].second.empty() && reference[1].second.empty());
    }
}

// An exported library holds no source text, only each source's path,
// digest and coverage facts, so covering code imported from it needs no
// source file: the producer's source is gone when the consumer elaborates.
void test_mapped_library_coverage(const TemporaryDirectory& directory)
{
    const auto producer_root = directory.path / "library-producer";
    const auto consumer_root = directory.path / "library-consumer";
    std::filesystem::create_directories(producer_root);
    std::filesystem::create_directories(consumer_root);
    const auto leaf = producer_root / "leaf.sv";
    write_text(leaf, R"(module leaf(input logic a);
  always @(a) begin
    // fsim coverage off metric=branch reason="library waiver"
    if (a) $display("LEAF_ONE"); else $display("LEAF_ZERO");
    // fsim coverage on metric=branch
  end
endmodule
)");
    fsim::project::Config producer;
    producer.base_directory = producer_root;
    producer.project.name = "coverage-library-producer";
    producer.project.top = "sv:vendor.leaf";
    producer.build.cache_path = producer_root / "cache";
    producer.coverage.enabled = true;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "vendor";
    sources.files = { leaf };
    producer.source_sets.push_back(std::move(sources));
    const auto library = directory.path / "coverage-vendor.fsimlib";
    fsim::diagnostic::Engine export_diagnostics;
    const auto exported = fsim::app::export_library(
        producer, "vendor", library, export_diagnostics);
    if (!exported) {
        fsim::diagnostic::print_text(std::cerr, export_diagnostics);
    }
    assert(exported);
    std::filesystem::remove(leaf);

    write_text(consumer_root / "tb.sv", R"(module tb;
  logic a = 0;
  leaf u(.a(a));
  initial begin #1 a = 1; #1 a = 0; end
endmodule
)");
    fsim::project::Config consumer;
    consumer.base_directory = consumer_root;
    consumer.project.name = "coverage-library-consumer";
    consumer.project.top = "sv:work.tb";
    consumer.project.time_resolution = "1ns";
    consumer.build.cache_path = consumer_root / "cache";
    consumer.coverage.enabled = true;
    consumer.library_mappings.push_back({ "vendor", library });
    consumer.elaboration.search_libraries = { "vendor" };
    fsim::project::SourceSet consumer_sources;
    consumer_sources.language = fsim::project::Language::system_verilog;
    consumer_sources.standard = "2017";
    consumer_sources.files = { consumer_root / "tb.sv" };
    consumer.source_sets.push_back(std::move(consumer_sources));
    fsim::diagnostic::Engine build_diagnostics;
    const auto built = fsim::app::build_project(consumer, build_diagnostics);
    if (!built) {
        fsim::diagnostic::print_text(std::cerr, build_diagnostics);
    }
    assert(built && built->code_coverage_enabled);
    const auto& inventory = built->design.code_coverage_inventory();
    assert(inventory);
    const auto leaf_source = std::ranges::find_if(inventory->sources,
        [](const auto& source) { return source.source_name.ends_with("leaf.sv"); });
    assert(leaf_source != inventory->sources.end());
    // The library's exclusion pragma came from its recorded coverage facts.
    const auto leaf_index = static_cast<std::size_t>(
        leaf_source - inventory->sources.begin());
    bool waived = false;
    for (const auto& instance : inventory->instances) {
        for (const auto& exclusion : instance.exclusions) {
            waived = waived
                || (exclusion.source_index == leaf_index
                    && exclusion.metric == fsim::runtime::CodeCoverageMetric::Branch
                    && exclusion.reason == "library waiver");
        }
    }
    assert(waived);
}

// Sets, or with a null value unsets, a variable of this process.
void set_environment(const char* const name, const char* const value)
{
#if defined(_WIN32)
    (void)::_putenv_s(name, value == nullptr ? "" : value);
#else
    (void)(value == nullptr ? ::unsetenv(name) : ::setenv(name, value, 1));
#endif
}

// The run record of a database $coverage_save wrote: (tick, delta).
std::pair<std::uint64_t, std::uint64_t> saved_run_time(
    const std::filesystem::path& path)
{
    const auto database = fsim::artifact::read_coverage_database(path);
    assert(database.ok());
    assert(database.contents->runs.size() == 1U);
    const auto& run = database.contents->runs.front();
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    return { run.final_tick, run.final_delta };
}

// $coverage_save saves the current state of coverage (IEEE 1800-2023
// 40.3.2.5): the database's run record carries the time of the call, 7 after
// the delay control (9.4.1), also when the call runs in a time step a closed
// static kernel runs ahead of the scheduler (docs/engine-v4-semantics.md
// S10). The library API (a kernel-owned Simulation) and the CLI, with and
// without the kernel's own time steps (FSIM_STATIC_KERNEL_TIME_WARP=0), agree
// with the reference interpreter.
void test_coverage_save_time(const TemporaryDirectory& directory)
{
    const auto root = directory.path / "coverage-save-time";
    std::filesystem::create_directories(root);
    const auto source = root / "coverage_save_time.sv";
    write_text(source, R"(module top;
  int status;
  initial begin
    #7 status = $coverage_save(`SV_COV_STATEMENT, "saved.fsimcov");
    #3 $finish;
  end
endmodule
)");
    const auto saved = root / "saved.fsimcov";
    fsim::project::Config config;
    config.base_directory = root;
    config.project.name = "coverage-save-time";
    config.project.top = "sv:work.top";
    config.project.tops = { { "sv:work.top", "top" } };
    config.project.time_resolution = "1ns";
    config.build.cache_path = root / "cache";
    config.coverage.enabled = true;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2005";
    sources.files = { source };
    config.source_sets.push_back(std::move(sources));
    const auto library_run = [&](const fsim::app::SimulationEngine engine,
                                 const bool warp) {
        set_environment("FSIM_STATIC_KERNEL_TIME_WARP", warp ? nullptr : "0");
        fsim::diagnostic::Engine diagnostics;
        auto built = fsim::app::build_project(config, diagnostics);
        assert(built && !diagnostics.has_error());
        fsim::app::Simulation simulation { std::move(*built), 1000U, engine };
        const auto result = simulation.run();
        assert(result.time == 10U);
        const auto status = simulation.find_signal("top.status");
        assert(status && simulation.read_signal(*status).low_word().aval == 1U);
        set_environment("FSIM_STATIC_KERNEL_TIME_WARP", nullptr);
        return saved_run_time(saved);
    };
    const auto reference
        = library_run(fsim::app::SimulationEngine::interpreter, true);
    assert(reference.first == 7U);
    for (const auto engine : { fsim::app::SimulationEngine::compiled,
             fsim::app::SimulationEngine::debug }) {
        for (const bool warp : { true, false }) {
            const auto actual = library_run(engine, warp);
            if (actual != reference) {
                std::cerr << "coverage saved at " << actual.first << '+'
                          << actual.second << ", not " << reference.first
                          << '+' << reference.second << '\n';
            }
            assert(actual == reference);
        }
    }

    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(root);
    const auto cli = [](std::vector<std::string> arguments) {
        std::vector<const char*> raw;
        raw.reserve(arguments.size());
        for (const auto& argument : arguments) {
            raw.push_back(argument.c_str());
        }
        std::istringstream input;
        std::ostringstream output;
        std::ostringstream error;
        const auto status = fsim::cli::run(static_cast<int>(raw.size()),
            raw.data(), fsim::app::make_cli_services(input), output, error);
        if (status != 0) {
            std::cerr << error.str() << output.str();
        }
        return status;
    };
    assert(cli({ "fsim", "compile", "--code-coverage", source.string() }) == 0);
    assert(cli({ "fsim", "elaborate", "--code-coverage", "--top", "sv:work.top" })
        == 0);
    for (const auto* engine : { "interpreter", "v4" }) {
        for (const bool warp : { true, false }) {
            set_environment(
                "FSIM_STATIC_KERNEL_TIME_WARP", warp ? nullptr : "0");
            assert(cli({ "fsim", "simulate", "--code-coverage", "--engine",
                       engine })
                == 0);
            set_environment("FSIM_STATIC_KERNEL_TIME_WARP", nullptr);
            const auto actual = saved_run_time(saved);
            if (actual != reference) {
                std::cerr << "CLI " << engine << " coverage saved at "
                          << actual.first << '+' << actual.second << '\n';
            }
            assert(actual == reference);
        }
    }
    std::filesystem::current_path(previous);
}

int main()
{
    TemporaryDirectory directory;
    write_text(directory.path / "top.sv",
        "module top; initial begin $finish; end endmodule\n");
    test_configuration_surface();
    test_cli_override(directory);
    test_disabled_build_has_no_runtime_state(directory);
    test_mapped_library_coverage(directory);
    test_coverage_save_time(directory);
    write_text(directory.path / "coverage_control.sv", R"(
module top;
  int start_status, stop_status, reset_status, check_status;
  int unsupported_status, invalid_status, unknown_status;
  int constant_start, constant_stop, constant_reset, constant_check;
  int constant_module, constant_hier, constant_assertion, constant_fsm;
  int constant_statement, constant_toggle;
  int constant_overflow, constant_error, constant_nocov;
  int constant_ok, constant_partial;
  int coverage_current, coverage_maximum;
  int coverage_merge, coverage_save, unsupported_get;
  initial begin
    constant_start = `SV_COV_START;
    constant_stop = `SV_COV_STOP;
    constant_reset = `SV_COV_RESET;
    constant_check = `SV_COV_CHECK;
    constant_module = `SV_COV_MODULE;
    constant_hier = `SV_COV_HIER;
    constant_assertion = `SV_COV_ASSERTION;
    constant_fsm = `SV_COV_FSM_STATE;
    constant_statement = `SV_COV_STATEMENT;
    constant_toggle = `SV_COV_TOGGLE;
    constant_overflow = `SV_COV_OVERFLOW;
    constant_error = `SV_COV_ERROR;
    constant_nocov = `SV_COV_NOCOV;
    constant_ok = `SV_COV_OK;
    constant_partial = `SV_COV_PARTIAL;
    stop_status = $coverage_control(`SV_COV_STOP, `SV_COV_STATEMENT, `SV_COV_MODULE, "top");
    start_status = $coverage_control(`SV_COV_START, `SV_COV_STATEMENT, `SV_COV_MODULE, "top");
    reset_status = $coverage_control(`SV_COV_RESET, `SV_COV_STATEMENT, `SV_COV_MODULE, "top");
    check_status = $coverage_control(`SV_COV_CHECK, `SV_COV_STATEMENT, `SV_COV_MODULE, "top");
    unsupported_status = $coverage_control(`SV_COV_CHECK, `SV_COV_ASSERTION, `SV_COV_MODULE, "top");
    invalid_status = $coverage_control(99, `SV_COV_STATEMENT, `SV_COV_MODULE, "top");
    unknown_status = $coverage_control(32'bx, `SV_COV_STATEMENT, `SV_COV_MODULE, "top");
    coverage_current = $coverage_get(`SV_COV_STATEMENT, `SV_COV_MODULE, "top");
    coverage_maximum = $coverage_get_max(`SV_COV_STATEMENT, `SV_COV_MODULE, "top");
    unsupported_get = $coverage_get(`SV_COV_ASSERTION, `SV_COV_MODULE, "top");
    coverage_merge = $coverage_merge(
        `SV_COV_STATEMENT, "standard.fsimcov");
    coverage_save = $coverage_save(
        `SV_COV_STATEMENT, "standard.fsimcov");
  end
endmodule
)");
    write_text(directory.path / "coverage_selection.sv",
        kCoverageSelectionSource);
    test_systemverilog_coverage_control(directory,
        fsim::project::Optimization::o0,
        fsim::app::SimulationEngine::interpreter, true);
    test_systemverilog_coverage_control(directory,
        fsim::project::Optimization::o0,
        fsim::app::SimulationEngine::compiled, true);
    test_systemverilog_coverage_control(directory,
        fsim::project::Optimization::o2,
        fsim::app::SimulationEngine::compiled, true);
    test_systemverilog_coverage_control(directory,
        fsim::project::Optimization::o0,
        fsim::app::SimulationEngine::interpreter, false);
    test_standard_coverage_selection(
        directory, fsim::app::SimulationEngine::interpreter);
    test_standard_coverage_selection(
        directory, fsim::app::SimulationEngine::compiled);
    test_counter_control_semantics();
    test_coverage_access_semantics();
    test_coverage_control_negative_profiles(directory);
    write_text(directory.path / "manifest_fsm.sv", R"(
module ctl(input logic clk);
  localparam logic [1:0] IDLE = 2'd0, RUN = 2'd1, DONE = 2'd2;
  logic [1:0] state = IDLE;
  always @(posedge clk)
    case (state)
      IDLE: state <= RUN;
      RUN: state <= DONE;
      DONE: state <= IDLE;
      default: state <= IDLE;
    endcase
endmodule
module mtb;
  logic clk = 0;
  ctl u (.clk(clk));
  initial begin
    repeat (4) begin #5 clk = 1; #5 clk = 0; end
    $finish;
  end
endmodule
)");
    write_text(directory.path / "manifest_fsm.vhd", R"(library ieee;
use ieee.std_logic_1164.all;
entity ctl is
  port (clk : in std_logic);
end entity;
architecture rtl of ctl is
  constant IDLE : std_logic_vector(1 downto 0) := "00";
  constant RUN : std_logic_vector(1 downto 0) := "01";
  constant DONE : std_logic_vector(1 downto 0) := "10";
  signal state : std_logic_vector(1 downto 0) := IDLE;
begin
  process (clk) begin
    if rising_edge(clk) then
      case state is
        when IDLE => state <= RUN;
        when RUN => state <= DONE;
        when DONE => state <= IDLE;
        when others => state <= IDLE;
      end case;
    end if;
  end process;
end architecture;
library ieee;
use ieee.std_logic_1164.all;
entity mtb is
end entity;
architecture sim of mtb is
  signal clk : std_logic := '0';
begin
  u : entity work.ctl port map (clk => clk);
  process begin
    for i in 1 to 4 loop
      wait for 5 ns; clk <= '1';
      wait for 5 ns; clk <= '0';
    end loop;
    wait;
  end process;
end architecture;
)");
    for (const bool vhdl : { false, true }) {
        test_configured_fsm_hint(directory, vhdl, "mtb.u");
        test_configured_fsm_hint(directory, vhdl, "mtb.other");
    }
}
