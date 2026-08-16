// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_random.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace {

bool has_code(
    const fsim::diagnostic::Engine& diagnostics,
    const std::string_view code)
{
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    using namespace fsim::systemc;

    const ScvIslandId island { 0x1020304050607080U, 0x90a0b0c0d0e0f001U };
    const ScvObjectId object { 0x1122334455667788U, 0x99aabbccddeeff00U };
    ScvRandomLimits limits;
    fsim::diagnostic::Engine diagnostics;
    const auto seeds = derive_scv_random_seeds(
        0x0123456789abcdefU, island, object, "top.worker[3]", 7U,
        limits, diagnostics);
    const auto repeated = derive_scv_random_seeds(
        0x0123456789abcdefU, island, object, "top.worker[3]", 7U,
        limits, diagnostics);
    assert(seeds && repeated == seeds && !diagnostics.has_error());
    assert(seeds->root == 0x0123456789abcdefU);
    assert(seeds->island == 0x56490519945c81f6U);
    assert(seeds->object == 0x602ea6fcca04c906U);
    assert(seeds->thread == 0x9a03f32426ae23d2U);

    fsim::diagnostic::Engine changed_diagnostics;
    const auto changed_thread = derive_scv_random_seeds(
        seeds->root, island, object, "top.worker[4]", 7U,
        limits, changed_diagnostics);
    const auto changed_ordinal = derive_scv_random_seeds(
        seeds->root, island, object, "top.worker[3]", 8U,
        limits, changed_diagnostics);
    const auto changed_object = derive_scv_random_seeds(
        seeds->root, island, { object.high, object.low + 1U },
        "top.worker[3]", 7U, limits, changed_diagnostics);
    assert(changed_thread && changed_ordinal && changed_object);
    assert(changed_thread->thread != seeds->thread);
    assert(changed_ordinal->thread != seeds->thread);
    assert(changed_object->object != seeds->object);

    ScvRandomStream stream { seeds->thread, limits };
    std::array<std::uint64_t, 4> first { };
    for (auto& value : first) {
        const auto generated = stream.next(diagnostics);
        assert(generated);
        value = *generated;
    }
    assert((first == std::array<std::uint64_t, 4> { 0xfea442c928317b1dU, 0xd84b1ccf736e8276U, 0x5f4238e91ef1d955U, 0x3b37c5418b796f94U }));
    ScvRandomStream identical { seeds->thread, limits };
    for (const auto expected : first) {
        assert(identical.next(diagnostics) == expected);
    }
    const auto replay_point = stream.snapshot();
    const auto after_snapshot = stream.next(diagnostics);
    assert(after_snapshot);
    assert(stream.restore(replay_point, diagnostics));
    assert(stream.next(diagnostics) == after_snapshot);

    const ScvRandomDomain domain {
        { { 9, 2U }, { -4, 1U }, { 17, 5U }, { 3, 3U } },
        { 9, 1000 }
    };
    const auto distribution = ScvRandomDistribution::create(
        domain, limits, diagnostics);
    assert(distribution && distribution->values().size() == 3U);
    assert(distribution->contains(-4));
    assert(distribution->contains(3));
    assert(distribution->contains(17));
    assert(!distribution->contains(9));
    for (std::size_t index = 0; index < 128U; ++index) {
        const auto value = distribution->draw(stream, diagnostics);
        assert(value && distribution->contains(*value));
    }

    auto bag = ScvRandomBag::create(domain, limits, diagnostics);
    assert(bag && bag->remaining() == 3U && bag->cycle() == 0U);
    std::vector<std::int64_t> cycle;
    while (bag->remaining() != 0U) {
        cycle.push_back(*bag->draw(stream, diagnostics));
    }
    assert(cycle.size() == 3U);
    assert(cycle[0] != cycle[1] && cycle[0] != cycle[2]
        && cycle[1] != cycle[2]);
    fsim::diagnostic::Engine empty_diagnostics;
    assert(!bag->draw(stream, empty_diagnostics));
    assert(has_code(empty_diagnostics, "FSIM-SCV-R002"));
    assert(bag->reset(diagnostics));
    assert(bag->remaining() == 3U && bag->cycle() == 1U);
    (void)bag->draw(stream, diagnostics);
    const auto bag_snapshot = bag->snapshot(stream);
    const auto replayed_value = bag->draw(stream, diagnostics);
    assert(replayed_value);
    assert(bag->restore(bag_snapshot, stream, diagnostics));
    assert(bag->draw(stream, diagnostics) == replayed_value);

    auto foreign_snapshot = bag_snapshot;
    ++foreign_snapshot.stream.seed;
    fsim::diagnostic::Engine replay_diagnostics;
    assert(!bag->restore(foreign_snapshot, stream, replay_diagnostics));
    assert(has_code(replay_diagnostics, "FSIM-SCV-R003"));
    auto corrupt_state = bag_snapshot;
    ++corrupt_state.stream.state;
    fsim::diagnostic::Engine corrupt_state_diagnostics;
    assert(!bag->restore(
        corrupt_state, stream, corrupt_state_diagnostics));
    assert(has_code(corrupt_state_diagnostics, "FSIM-SCV-R003"));
    auto duplicate_snapshot = bag_snapshot;
    duplicate_snapshot.remaining = { -4, -4 };
    fsim::diagnostic::Engine duplicate_snapshot_diagnostics;
    assert(!bag->restore(
        duplicate_snapshot, stream, duplicate_snapshot_diagnostics));
    assert(has_code(duplicate_snapshot_diagnostics, "FSIM-SCV-R002"));
    auto unordered_snapshot = bag_snapshot;
    unordered_snapshot.remaining = { 17, -4 };
    fsim::diagnostic::Engine unordered_snapshot_diagnostics;
    assert(!bag->restore(
        unordered_snapshot, stream, unordered_snapshot_diagnostics));
    assert(has_code(unordered_snapshot_diagnostics, "FSIM-SCV-R002"));

    fsim::diagnostic::Engine identity_diagnostics;
    assert(!derive_scv_random_seeds(
        1U, { }, object, "thread", 0U, limits, identity_diagnostics));
    assert(has_code(identity_diagnostics, "FSIM-SCV-R001"));
    auto short_identity_limits = limits;
    short_identity_limits.max_identity_bytes = 3U;
    fsim::diagnostic::Engine long_identity_diagnostics;
    assert(!derive_scv_random_seeds(1U, island, object, "thread", 0U,
        short_identity_limits, long_identity_diagnostics));
    assert(has_code(long_identity_diagnostics, "FSIM-SCV-R001"));

    fsim::diagnostic::Engine domain_diagnostics;
    assert(!ScvRandomDistribution::create({ }, limits, domain_diagnostics));
    assert(has_code(domain_diagnostics, "FSIM-SCV-R003"));
    fsim::diagnostic::Engine duplicate_diagnostics;
    assert(!ScvRandomDistribution::create(
        { { { 1, 1U }, { 1, 2U } }, { } }, limits,
        duplicate_diagnostics));
    assert(has_code(duplicate_diagnostics, "FSIM-SCV-R002"));
    fsim::diagnostic::Engine zero_weight_diagnostics;
    assert(!ScvRandomDistribution::create(
        { { { 1, 0U } }, { } }, limits, zero_weight_diagnostics));
    assert(has_code(zero_weight_diagnostics, "FSIM-SCV-R002"));
    fsim::diagnostic::Engine overflow_diagnostics;
    assert(!ScvRandomDistribution::create(
        { { { 1, std::numeric_limits<std::uint64_t>::max() }, { 2, 1U } }, { } },
        limits, overflow_diagnostics));
    assert(has_code(overflow_diagnostics, "FSIM-SCV-R002"));
    fsim::diagnostic::Engine exclusion_diagnostics;
    assert(!ScvRandomDistribution::create(
        { { { 1, 1U } }, { 1 } }, limits, exclusion_diagnostics));
    assert(has_code(exclusion_diagnostics, "FSIM-SCV-R002"));

    auto one_draw_limits = limits;
    one_draw_limits.max_draws = 1U;
    ScvRandomStream limited { 5U, one_draw_limits };
    assert(limited.next(diagnostics));
    fsim::diagnostic::Engine resource_diagnostics;
    assert(!limited.next(resource_diagnostics));
    assert(has_code(resource_diagnostics, "FSIM-SCV-R003"));
    fsim::diagnostic::Engine bound_diagnostics;
    assert(!stream.bounded(0U, bound_diagnostics));
    assert(has_code(bound_diagnostics, "FSIM-SCV-R002"));
}
