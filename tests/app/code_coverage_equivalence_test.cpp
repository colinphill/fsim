// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_internal.hpp"

#include "fsim/elaboration/coverage_inventory.hpp"
#include "fsim/elaboration/verilog_coverage_points.hpp"
#include "fsim/elaboration/vhdl_coverage_points.hpp"
#include "fsim/frontend/parser.hpp"
#include "fsim/runtime/coverage_aggregation.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace fsim;

constexpr std::string_view kVerilogSource = R"(module verilog_leaf;
  reg value;
  initial value = 1'b1;
endmodule
)";

constexpr std::string_view kSystemVerilogSource = R"(module sv_leaf;
  logic value;
  initial value = 1'b1;
endmodule
)";

constexpr std::string_view kVhdlSource = R"(entity vhdl_leaf is end vhdl_leaf;
architecture rtl of vhdl_leaf is
begin
  worker : process
  begin
    report "covered" severity note;
    wait;
  end process;
end rtl;
)";

std::span<const std::byte> bytes(const std::string_view text)
{
    return std::as_bytes(std::span { text.data(), text.size() });
}

frontend::CodeCoverageSourceIdentity source_identity(
    const std::string_view logical_name, const std::string_view contents)
{
    const auto root = std::filesystem::path { "/coverage-checkout" };
    auto result = frontend::make_code_coverage_source_identity(
        root, root / logical_name, bytes(contents));
    assert(result.ok());
    return std::move(*result.identity);
}

struct DiscoveredSourcePoint {
    elaboration::CoverageInventorySource source;
    runtime::CodeCoveragePointId point;
    frontend::CodeCoverageSourceSpan span;
    std::uint64_t line { };
    frontend::Language language { frontend::Language::Verilog2005 };
    std::string standard;
};

DiscoveredSourcePoint discover_verilog(
    const std::string_view logical_name, const std::string_view contents,
    const frontend::Language language, std::string standard)
{
    auto identity = source_identity(logical_name, contents);
    elaboration::VerilogCoverageSource source {
        std::string { logical_name }, identity
    };
    const auto parsed = frontend::parse_text(
        source.source_name, contents, language);
    assert(parsed.ok() && parsed.design.units.size() == 1U
        && parsed.design.units.front().processes.size() == 1U);
    const auto discovered = elaboration::discover_verilog_statement_points(
        parsed.design.units.front().processes.front().statements,
        language, std::span { &source, 1U });
    assert(discovered.ok() && discovered.points.size() == 1U);
    return {
        { source.source_name, std::move(identity) },
        discovered.points.front().id,
        discovered.points.front().span,
        discovered.points.front().line,
        language,
        std::move(standard),
    };
}

DiscoveredSourcePoint discover_vhdl()
{
    constexpr std::string_view logical_name = "rtl/vhdl_leaf.vhd";
    auto identity = source_identity(logical_name, kVhdlSource);
    elaboration::VhdlCoverageSource source {
        std::string { logical_name }, identity
    };
    const auto parsed = frontend::parse_text(
        source.source_name, kVhdlSource, frontend::Language::Vhdl2008,
        frontend::VhdlStandard::Vhdl2008);
    assert(parsed.ok() && parsed.design.units.size() == 2U
        && parsed.design.units.back().processes.size() == 1U);
    const auto discovered = elaboration::discover_vhdl_statement_points(
        parsed.design.units.back().processes.front().statements,
        frontend::Language::Vhdl2008, frontend::VhdlStandard::Vhdl2008,
        std::span { &source, 1U });
    assert(discovered.ok() && discovered.points.size() == 2U);
    return {
        { source.source_name, std::move(identity) },
        discovered.points.front().id,
        discovered.points.front().span,
        discovered.points.front().line,
        frontend::Language::Vhdl2008,
        "2008",
    };
}

struct Corpus {
    std::vector<DiscoveredSourcePoint> discovered;
    std::vector<elaboration::CoverageInventorySource> sources;
    std::vector<elaboration::CoverageInventoryOwner> owners;
    std::vector<elaboration::CoverageInstanceInventoryDraft> drafts;
    elaboration::CodeCoverageInventory inventory;
};

Corpus make_corpus()
{
    Corpus corpus;
    corpus.discovered = {
        discover_verilog("rtl/verilog_leaf.v", kVerilogSource,
            frontend::Language::Verilog2005, "2005"),
        discover_verilog("rtl/sv_leaf.sv", kSystemVerilogSource,
            frontend::Language::SystemVerilog2017, "2017"),
        discover_vhdl(),
    };
    for (const auto& source : corpus.discovered) {
        corpus.sources.push_back(source.source);
    }

    constexpr std::array instances {
        "top.verilog_a", "top.verilog_b",
        "top.sv_a", "top.sv_b",
        "top.vhdl_a", "top.vhdl_b",
    };
    constexpr std::array units {
        "verilog_leaf", "verilog_leaf",
        "sv_leaf", "sv_leaf",
        "vhdl_leaf", "vhdl_leaf",
    };
    corpus.owners.reserve(instances.size());
    corpus.drafts.reserve(instances.size());
    for (std::uint32_t index = 0U; index < instances.size(); ++index) {
        const auto source_index = static_cast<std::size_t>(index / 2U);
        const auto& source = corpus.discovered[source_index];
        corpus.owners.push_back({
            index, instances[index], source.language,
            source.source.source_name, { }, "work", units[index], { },
        });
        corpus.drafts.push_back({
            index,
            { {
                source.point, runtime::CodeCoverageMetric::Statement,
                source_index, source.span, source.line,
            } },
        });
    }
    const auto built = elaboration::make_code_coverage_inventory(
        corpus.sources, corpus.drafts, corpus.owners);
    assert(built.ok() && built.inventory->total_points == instances.size());
    corpus.inventory = *built.inventory;
    return corpus;
}

runtime::CodeCoverageRun make_run(
    const elaboration::CodeCoverageInventory& inventory)
{
    runtime::CodeCoverageRun run;
    run.id = { 0xc0dec0dec0dec0deULL, 0x1780000000000019ULL };
    for (const auto& instance : inventory.instances) {
        for (const auto& point : instance.points) {
            run.points.push_back(point.point);
        }
    }
    return run;
}

runtime::CodeCoverageResult make_result(
    const runtime::CodeCoverageRun& run,
    const std::span<const std::uint64_t> counters)
{
    assert(counters.size() == run.points.size());
    runtime::CodeCoverageResult result;
    result.run = run.id;
    std::uint64_t covered { };
    for (std::size_t index = 0U; index < run.points.size(); ++index) {
        const auto hits = counters[index];
        if (hits != 0U) {
            ++covered;
        }
        result.points.push_back({
            run.points[index].id,
            run.points[index].metric,
            run.points[index].counter,
            hits,
            hits == 0U ? runtime::CodeCoverageStatus::Uncovered
                       : runtime::CodeCoverageStatus::Covered,
        });
    }
    result.metrics.push_back({
        runtime::CodeCoverageMetric::Statement,
        static_cast<std::uint64_t>(run.points.size()),
        covered,
        0U,
        static_cast<std::uint64_t>(run.points.size()) - covered,
        0U,
        runtime::CodeCoverageStatus::Partial,
    });
    assert(runtime::validate_code_coverage(run, result).ok());
    return result;
}

struct Snapshot {
    std::vector<std::uint64_t> counters;
    std::vector<std::uint64_t> exact;

    friend bool operator==(const Snapshot&, const Snapshot&) = default;
};

Snapshot capture(
    const Corpus& corpus,
    std::optional<project::Optimization> optimization,
    const bool debug_engine = false)
{
    const auto run = make_run(corpus.inventory);
    runtime::simir::Interpreter interpreter;
    interpreter.set_code_coverage_counters(
        std::vector<std::uint64_t>(corpus.inventory.total_points));
    std::unique_ptr<compiler::LlvmJit> jit;
    if (optimization) {
        compiler::LlvmJitOptions options;
        options.optimization = app::application_detail::jit_optimization(
            *optimization);
        options.debug_instrumentation = debug_engine;
        jit = std::make_unique<compiler::LlvmJit>(std::move(options));
    }

    constexpr std::array executed_instances { 0U, 2U, 4U };
    constexpr std::array symbols {
        "coverage_equivalence_verilog",
        "coverage_equivalence_systemverilog",
        "coverage_equivalence_vhdl",
    };
    const std::array<std::uint32_t, 0U> no_signal_widths { };
    std::size_t debug_points { };
    interpreter.set_execution_point_hook(
        [&](runtime::Scheduler&,
            const runtime::simir::ExecutionPoint&) { ++debug_points; });
    for (std::size_t language = 0U;
         language < executed_instances.size(); ++language) {
        const auto instance_index = executed_instances[language];
        const auto& inventory_instance
            = corpus.inventory.instances[instance_index];
        const auto& discovered = corpus.discovered[language];
        runtime::simir::Process process;
        process.id = static_cast<runtime::simir::ProcessId>(language);
        process.name = std::string { symbols[language] };
        process.language_standard = discovered.standard;
        process.compatibility_profile = discovered.language
                == frontend::Language::Vhdl2008
            ? "fsim-vhdl-standard"
            : "none";
        process.operations = {
            runtime::simir::DebugPoint {
                runtime::simir::DebugPointKind::statement,
                { discovered.source.source_name,
                    static_cast<std::uint32_t>(discovered.line), 1U },
                std::string { corpus.owners[instance_index].instance },
            },
            runtime::simir::CodeCoverageHit {
                inventory_instance.points.front().point.id,
                runtime::CodeCoverageMetric::Statement,
                inventory_instance.points.front().point.counter,
            },
            runtime::simir::Halt { },
        };
        std::optional<compiler::JitProcessHandle> handle;
        if (jit) {
            jit->add_process(symbols[language], process, no_signal_widths);
            handle = jit->lookup(symbols[language]);
        }
        const auto process_id = interpreter.add_process(std::move(process));
        if (jit) {
            interpreter.set_process_executor(
                process_id,
                std::make_unique<app::application_detail::LlvmProcessExecutor>(
                    *jit, *handle, interpreter.process_program(process_id),
                    no_signal_widths,
                    std::span<const runtime::simir::ValueKind> { },
                    std::span<const runtime::simir::ResolutionKind> { }));
        }
    }
    const auto execution = interpreter.run();
    assert(execution.status == runtime::RunStatus::completed);
    assert(debug_points >= executed_instances.size());

    const auto result = make_result(
        run, interpreter.code_coverage_counters());
    std::vector<runtime::CodeCoveragePointOwnership> ownership;
    ownership.reserve(corpus.inventory.total_points);
    for (std::uint32_t instance = 0U;
         instance < corpus.inventory.instances.size(); ++instance) {
        for (const auto& point : corpus.inventory.instances[instance].points) {
            ownership.push_back({
                point.point.counter, instance, point.source_index,
            });
        }
    }
    const auto aggregated = runtime::make_code_coverage_aggregation(
        run, result, ownership, corpus.inventory.instances.size(),
        corpus.inventory.sources.size());
    assert(aggregated.ok());

    Snapshot snapshot;
    snapshot.counters.assign(
        interpreter.code_coverage_counters().begin(),
        interpreter.code_coverage_counters().end());
    for (const auto& point : result.points) {
        snapshot.exact.insert(snapshot.exact.end(), {
            point.point.high, point.point.low, point.counter.value,
            point.hits, static_cast<std::uint64_t>(point.status),
        });
    }
    for (const auto& instance : aggregated.aggregation->instances) {
        snapshot.exact.push_back(instance.instance);
        for (const auto& point : instance.points) {
            snapshot.exact.insert(snapshot.exact.end(), {
                point.counter.value, point.hits,
                static_cast<std::uint64_t>(point.status),
            });
        }
    }
    for (const auto& source : aggregated.aggregation->sources) {
        snapshot.exact.push_back(source.source);
        assert(source.points.size() == 1U);
        const auto& point = source.points.front();
        assert(point.hits == 1U && point.occurrences == 2U
            && point.covered_occurrences == 1U
            && point.uncovered_occurrences == 1U
            && point.status == runtime::CodeCoverageStatus::Covered);
        snapshot.exact.insert(snapshot.exact.end(), {
            point.point.high, point.point.low, point.hits,
            point.occurrences, point.covered_occurrences,
            point.uncovered_occurrences,
            static_cast<std::uint64_t>(point.status),
        });
    }
    return snapshot;
}

} // namespace

int main()
{
    const auto corpus = make_corpus();
    const auto reference = capture(corpus, std::nullopt);
    assert((reference.counters
        == std::vector<std::uint64_t> { 1U, 0U, 1U, 0U, 1U, 0U }));
    for (const auto optimization : {
             project::Optimization::o0,
             project::Optimization::o1,
             project::Optimization::o2,
             project::Optimization::o3,
         }) {
        assert(capture(corpus, optimization) == reference);
    }
    assert(capture(corpus, project::Optimization::o0, true) == reference);
}
