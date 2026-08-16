// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_constraints.hpp"
#include "fsim/systemc/scv_random.hpp"
#include "fsim/systemc/scv_smart_ptr.hpp"

#include <cassert>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

using fsim::systemc::ScvIslandId;
using fsim::systemc::ScvObjectId;
using fsim::systemc::ScvRandomStream;

struct Worker {
    ScvRandomStream stream;
    std::vector<std::uint64_t> values;
};

Worker make_worker(
    const std::uint64_t root_seed,
    const ScvIslandId island,
    const ScvObjectId object,
    const std::string_view thread)
{
    fsim::diagnostic::Engine diagnostics;
    const auto seeds = fsim::systemc::derive_scv_random_seeds(
        root_seed, island, object, thread, 0U, { }, diagnostics);
    assert(seeds && !diagnostics.has_error());
    return { ScvRandomStream { seeds->thread }, { } };
}

void draw(Worker& worker)
{
    fsim::diagnostic::Engine diagnostics;
    const auto value = worker.stream.next(diagnostics);
    assert(value && !diagnostics.has_error());
    worker.values.push_back(*value);
}

} // namespace

int main()
{
    const ScvIslandId island_a { 1U, 10U };
    const ScvIslandId island_b { 2U, 20U };
    const ScvObjectId object_a { 3U, 30U };
    const ScvObjectId object_b { 4U, 40U };
    constexpr std::uint64_t root_seed = 0x5eed173U;

    auto first_a = make_worker(root_seed, island_a, object_a, "top.a.thread");
    auto first_b = make_worker(root_seed, island_b, object_b, "top.b.thread");
    for (std::size_t index = 0; index < 8U; ++index) {
        draw(first_a);
        draw(first_b);
    }

    auto reordered_a = make_worker(root_seed, island_a, object_a, "top.a.thread");
    auto reordered_b = make_worker(root_seed, island_b, object_b, "top.b.thread");
    for (std::size_t index = 0; index < 8U; ++index) {
        draw(reordered_b);
    }
    for (std::size_t index = 0; index < 8U; ++index) {
        draw(reordered_a);
    }

    assert(first_a.values == reordered_a.values);
    assert(first_b.values == reordered_b.values);
    assert(first_a.values != first_b.values);

    auto extra_worker = make_worker(
        root_seed, island_a, { 5U, 50U }, "top.unrelated.thread");
    for (std::size_t index = 0; index < 32U; ++index) {
        draw(extra_worker);
    }
    auto isolated_a = make_worker(root_seed, island_a, object_a, "top.a.thread");
    for (std::size_t index = 0; index < 8U; ++index) {
        draw(isolated_a);
    }
    assert(first_a.values == isolated_a.values);

    const auto native_baseline = fsim::systemc::ScvNativeSmartPtrRegistry::live_native_payloads();
    {
        fsim::systemc::ScvNativeSmartPtrRegistry native;
        fsim::diagnostic::Engine diagnostics;
        const auto aggregate = native.create(
            fsim::systemc::ScvNativeValueKind::aggregate, object_a,
            "top.a.native", first_a.values.front(), diagnostics);
        assert(aggregate && !diagnostics.has_error());
        const auto copy = native.copy(*aggregate, diagnostics);
        assert(copy && native.live_handles() == 2U);
        assert(fsim::systemc::ScvNativeSmartPtrRegistry::live_native_payloads()
            == native_baseline + 1U);
        const std::vector<fsim::systemc::ScvNativeExtensionStep> scalar_path {
            { fsim::systemc::ScvNativeExtensionStepKind::field, 0U }
        };
        fsim::systemc::ScvConstraintRequest constraint;
        constraint.variables = {
            { "top.a.native.scalar", *aggregate, scalar_path, { 2, 4, 6 } }
        };
        constraint.clauses = { { "top.a.native.scalar-is-four", { { 0U, 1 } },
            fsim::systemc::ScvConstraintRelation::equal, 4, false } };
        constraint.selection = first_a.values[1];
        const auto constrained = fsim::systemc::solve_scv_constraints(
            native, constraint, diagnostics);
        assert(constrained.status
            == fsim::systemc::ScvConstraintStatus::satisfied);
        assert(native.extension(*copy, scalar_path, diagnostics)->signed_value
            == 4);
        assert(native.release(*aggregate, diagnostics));
        assert(native.info(*copy, diagnostics));
    }
    assert(fsim::systemc::ScvNativeSmartPtrRegistry::live_native_payloads()
        == native_baseline);
}
