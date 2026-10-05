// SPDX-License-Identifier: Apache-2.0

#include "../../src/runtime/simir_internal.hpp"
#include "runtime_fused_staging_failure_support.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

struct OwnedDriverDemotionTestAccess {
    static auto& implementation(Interpreter& interpreter)
    {
        return *interpreter.impl_;
    }
};

} // namespace fsim::runtime::simir

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
using namespace fsim::tests::runtime::staging_failure_support;

constexpr std::size_t maximum_allocation_sites = 1024U;
constexpr std::size_t partial_force_width = 13U;
constexpr std::size_t partial_force_offset = 11U;

enum class ForceOperation : std::uint8_t {
    whole_force,
    partial_force,
    whole_release,
    partial_release
};

void require(const bool condition, const char* const message)
{
    if (!condition) {
        throw std::runtime_error { message };
    }
}

PackedLogic4 filled(const std::size_t width, const Logic4 state)
{
    return PackedLogic4 { width, state };
}

struct AliasFamily {
    Interpreter interpreter { };
    std::size_t width { };
    PackedLogic4 whole_ones { 0U, Logic4::zero };
    PackedLogic4 partial_ones { 0U, Logic4::zero };
    PackedLogic4 sibling_ones { 0U, Logic4::zero };
    SignalId first { };
    SignalId second { };
    SignalId proxy { };
    ContainerObjectId object { };

    explicit AliasFamily(const std::size_t element_width)
        : width(element_width)
    {
        whole_ones = filled(width * 2U, Logic4::one);
        partial_ones = filled(partial_force_width, Logic4::one);
        sibling_ones = filled(width, Logic4::one);
        const auto zeros = filled(width, Logic4::zero);
        first = interpreter.add_signal({
            "words[1]", zeros, ResolutionKind::sv_wire });
        second = interpreter.add_signal({
            "words[0]", zeros, ResolutionKind::sv_wire });
        proxy = interpreter.add_signal({
            "words", filled(width * 2U, Logic4::zero),
            ResolutionKind::sv_wire });

        ContainerType type;
        type.fixed = true;
        type.element_width = static_cast<std::uint32_t>(width);
        type.index_left = 1;
        type.index_right = 0;
        type.dimensions = { { 1, 0 } };
        object = interpreter.add_container_object({
            "words",
            ContainerValue { type, { zeros, zeros }, { } },
            std::nullopt });
        interpreter.add_container_element_signal_alias(
            { object, 0U, first, true, true });
        interpreter.add_container_element_signal_alias(
            { object, 1U, second, true, true });
        interpreter.add_container_aggregate_signal_alias(
            { object, proxy, true, true });
        interpreter.start();
    }

    [[nodiscard]] std::size_t partial_offset() const noexcept
    {
        // `first` is the high element and occupies the upper aggregate lane.
        return width + partial_force_offset;
    }
};

struct LeafSnapshot {
    std::optional<PackedLogic4> forced_value;
    std::optional<PackedLogic4> forced_mask;
    PackedLogic4 current { 0U, Logic4::zero };
    PackedLogic4 stored { 0U, Logic4::zero };
    PackedLogic4 previous { 0U, Logic4::zero };

    friend bool operator==(const LeafSnapshot&, const LeafSnapshot&) = default;
};

struct FamilySnapshot {
    std::array<LeafSnapshot, 2U> leaves;
    PackedLogic4 proxy_current { 0U, Logic4::zero };
    PackedLogic4 proxy_stored { 0U, Logic4::zero };
    PackedLogic4 proxy_previous { 0U, Logic4::zero };
    std::array<PackedLogic4, 2U> container_elements {
        PackedLogic4 { 0U, Logic4::zero },
        PackedLogic4 { 0U, Logic4::zero }
    };
    bool proxy_forced { };
    bool planes_match { };
    bool frames_clear { };

    friend bool operator==(const FamilySnapshot&, const FamilySnapshot&)
        = default;
};

template<class Implementation>
bool current_wide_planes_match(
    const Implementation& impl,
    const SignalId signal,
    const PackedLogic4& value)
{
    const auto word_count = (static_cast<std::size_t>(value.width()) + 63U)
        / 64U;
    const auto offset = static_cast<std::size_t>(
        impl.direct_wide_signal_offsets.at(signal));
    if (offset > impl.direct_wide_signal_aval.size()
        || word_count > impl.direct_wide_signal_aval.size() - offset
        || offset > impl.direct_wide_signal_bval.size()
        || word_count > impl.direct_wide_signal_bval.size() - offset) {
        return false;
    }
    for (std::size_t word = 0U; word < word_count; ++word) {
        std::uint64_t expected_aval { };
        std::uint64_t expected_bval { };
        const auto bit_begin = word * 64U;
        const auto bit_end = std::min(
            static_cast<std::size_t>(value.width()), bit_begin + 64U);
        for (std::size_t bit = bit_begin; bit < bit_end; ++bit) {
            const auto logic = value.get(bit);
            const auto mask = UINT64_C(1) << (bit % 64U);
            if (logic == Logic4::one || logic == Logic4::x) {
                expected_aval |= mask;
            }
            if (logic == Logic4::x || logic == Logic4::z) {
                expected_bval |= mask;
            }
        }
        if (impl.direct_wide_signal_aval[offset + word] != expected_aval
            || impl.direct_wide_signal_bval[offset + word] != expected_bval) {
            return false;
        }
    }
    return true;
}

FamilySnapshot snapshot(AliasFamily& family)
{
    auto& impl = OwnedDriverDemotionTestAccess::implementation(
        family.interpreter);
    const std::array<SignalId, 2U> leaves { family.first, family.second };
    FamilySnapshot result;
    result.planes_match = true;
    for (std::size_t ordinal = 0U; ordinal < leaves.size(); ++ordinal) {
        const auto signal = leaves[ordinal];
        auto& leaf = result.leaves[ordinal];
        if (impl.forced_values.at(signal)) {
            leaf.forced_value = *impl.forced_values.at(signal);
        }
        if (impl.forced_masks.at(signal)) {
            leaf.forced_mask = *impl.forced_masks.at(signal);
        }
        require(leaf.forced_value.has_value() == leaf.forced_mask.has_value(),
            "a force value and its mask are installed or cleared together");
        leaf.current = family.interpreter.signal_value(signal);
        leaf.stored = family.interpreter.stored_signal_value(signal);
        leaf.previous = impl.signal_last_values.at(signal);
        result.planes_match = result.planes_match
            && current_wide_planes_match(impl, signal, leaf.current);
    }
    result.proxy_current = family.interpreter.signal_value(family.proxy);
    result.proxy_stored = family.interpreter.stored_signal_value(family.proxy);
    result.proxy_previous = impl.aggregate_signal_last_value(family.proxy);
    const auto& container
        = family.interpreter.container_object_value(family.object);
    require(container.elements.size() == result.container_elements.size(),
        "the logical container retains both elements");
    result.container_elements[0U] = container.elements[0U];
    result.container_elements[1U] = container.elements[1U];
    auto current_projection = filled(family.width * 2U, Logic4::zero);
    current_projection.insert_bits(result.leaves[0U].current, family.width);
    current_projection.insert_bits(result.leaves[1U].current, 0U);
    auto stored_projection = filled(family.width * 2U, Logic4::zero);
    stored_projection.insert_bits(result.leaves[0U].stored, family.width);
    stored_projection.insert_bits(result.leaves[1U].stored, 0U);
    // An aggregate LAST is the whole value preceding its latest change.
    // Unchanged leaves retain their own older LAST values independently.
    require(result.proxy_current == current_projection
            && result.proxy_stored == stored_projection
            && result.container_elements[0U] == result.leaves[0U].current
            && result.container_elements[1U] == result.leaves[1U].current,
        "leaf, aggregate current/stored and retained-container views agree");
    result.proxy_forced = family.interpreter.signal_is_forced(family.proxy);
    result.frames_clear
        = impl.aggregate_signal_batches.at(family.proxy).depth == 0U
        && impl.container_alias_write_batches.at(family.object).frames.empty();
    return result;
}

void set_range(PackedLogic4& value,
    const std::size_t offset,
    const std::size_t width,
    const Logic4 state)
{
    for (std::size_t bit = 0U; bit < width; ++bit) {
        value.set(offset + bit, state);
    }
}

void require_masked_leaf(const LeafSnapshot& leaf,
    const PackedLogic4& expected_mask,
    const PackedLogic4& expected_value,
    const char* const message)
{
    require(leaf.forced_mask.has_value() && leaf.forced_value.has_value()
            && *leaf.forced_mask == expected_mask
            && *leaf.forced_value == expected_value,
        message);
}

void require_operation_state(const FamilySnapshot& state,
    const std::size_t width,
    const ForceOperation operation)
{
    auto first_current = filled(width, Logic4::zero);
    auto second_current = filled(width, Logic4::zero);
    auto first_mask = filled(width, Logic4::zero);
    auto second_mask = filled(width, Logic4::zero);
    auto first_force = filled(width, Logic4::zero);
    auto second_force = filled(width, Logic4::zero);
    bool first_forced { };
    bool second_forced { };

    switch (operation) {
    case ForceOperation::whole_force:
        first_current.fill(Logic4::one);
        second_current.fill(Logic4::one);
        first_mask.fill(Logic4::one);
        second_mask.fill(Logic4::one);
        first_force.fill(Logic4::one);
        second_force.fill(Logic4::one);
        first_forced = true;
        second_forced = true;
        break;
    case ForceOperation::partial_force:
        set_range(first_current, partial_force_offset,
            partial_force_width, Logic4::one);
        second_current.fill(Logic4::one);
        set_range(first_mask, partial_force_offset,
            partial_force_width, Logic4::one);
        first_force = first_current;
        second_mask.fill(Logic4::one);
        second_force.fill(Logic4::one);
        first_forced = true;
        second_forced = true;
        break;
    case ForceOperation::whole_release:
        break;
    case ForceOperation::partial_release:
        first_current.fill(Logic4::one);
        second_current.fill(Logic4::one);
        first_mask.fill(Logic4::one);
        set_range(first_mask, partial_force_offset,
            partial_force_width, Logic4::zero);
        first_force.fill(Logic4::one);
        second_mask.fill(Logic4::one);
        second_force.fill(Logic4::one);
        set_range(first_current, partial_force_offset,
            partial_force_width, Logic4::zero);
        first_forced = true;
        second_forced = true;
        break;
    }

    require(state.leaves[0U].current == first_current
            && state.leaves[1U].current == second_current
            && state.proxy_forced == (first_forced || second_forced),
        "the completed operation has the requested selected current bits");
    if (first_forced) {
        require_masked_leaf(state.leaves[0U], first_mask, first_force,
            "the first leaf has the exact force mask and forced value");
    } else {
        require(!state.leaves[0U].forced_mask
                && !state.leaves[0U].forced_value,
            "the first leaf has no residual force after whole release");
    }
    if (second_forced) {
        require_masked_leaf(state.leaves[1U], second_mask, second_force,
            "the sibling leaf retains its exact force mask and value");
    } else {
        require(!state.leaves[1U].forced_mask
                && !state.leaves[1U].forced_value,
            "the sibling leaf has no residual force after whole release");
    }
}

void apply_operation(AliasFamily& family, const ForceOperation operation)
{
    switch (operation) {
    case ForceOperation::whole_force:
        family.interpreter.force_signal(family.proxy, family.whole_ones);
        break;
    case ForceOperation::partial_force:
        family.interpreter.force_signal_slice(
            family.proxy, family.partial_ones, family.partial_offset());
        break;
    case ForceOperation::whole_release:
        family.interpreter.release_signal(family.proxy);
        break;
    case ForceOperation::partial_release:
        family.interpreter.release_signal_slice(
            family.proxy, family.partial_offset(), partial_force_width);
        break;
    }
}

void prepare_before_operation(
    AliasFamily& family, const ForceOperation operation)
{
    switch (operation) {
    case ForceOperation::whole_force:
        break;
    case ForceOperation::partial_force:
        // Seed the lower sibling outside the injected write. The selected
        // force range affects only the high leaf.
        family.interpreter.force_signal_slice(
            family.proxy, family.sibling_ones, 0U);
        break;
    case ForceOperation::whole_release:
    case ForceOperation::partial_release:
        // Release failures must retain these pre-existing masks on every leaf.
        family.interpreter.force_signal(family.proxy, family.whole_ones);
        break;
    }
}

bool force_operation(const ForceOperation operation) noexcept
{
    return operation == ForceOperation::whole_force
        || operation == ForceOperation::partial_force;
}

void perform_follow_up(AliasFamily& family, const ForceOperation operation)
{
    if (force_operation(operation)) {
        family.interpreter.release_signal(family.proxy);
    } else {
        family.interpreter.force_signal(family.proxy, family.whole_ones);
    }
    const auto after_follow_up = snapshot(family);
    require_operation_state(
        after_follow_up, family.width,
        force_operation(operation)
            ? ForceOperation::whole_release
            : ForceOperation::whole_force);
    require(after_follow_up.frames_clear && after_follow_up.planes_match,
        "a distinct follow-up force/release leaves no stale frame or plane");
}

std::size_t measure_operation(const std::size_t width,
    const ForceOperation operation)
{
    auto family = AliasFamily { width };
    prepare_before_operation(family, operation);
    // Match the injected iterations' cache state and retained COW references.
    const auto before = snapshot(family);
    require(before.frames_clear && before.planes_match,
        "allocation measurement begins from the same coherent snapshot");
    begin_allocation_count();
    apply_operation(family, operation);
    const auto count = end_allocation_count();
    require(count != 0U,
        "each whole and partial force/release route exercises allocations");
    require(count <= maximum_allocation_sites,
        "the force/release allocation sweep stays within its site cap");
    return count;
}

void sweep_operation(const std::size_t width,
    const ForceOperation operation,
    const char* const label)
{
    auto baseline_family = AliasFamily { width };
    prepare_before_operation(baseline_family, operation);
    const auto before = snapshot(baseline_family);

    auto expected_family = AliasFamily { width };
    prepare_before_operation(expected_family, operation);
    apply_operation(expected_family, operation);
    const auto expected = snapshot(expected_family);
    require(before != expected,
        "the chosen force/release operation changes observable family state");
    require_operation_state(expected, width, operation);
    require(before.frames_clear && expected.frames_clear
            && before.planes_match && expected.planes_match,
        "prepared baseline and successful family states have clean frames and planes");

    const auto allocation_count = measure_operation(width, operation);
    std::size_t injected_failures { };
    std::size_t unchanged_failures { };
    std::size_t completed_failures { };
    bool successful_probe { };
    for (std::size_t failure_index = 0U;
        failure_index <= allocation_count;
        ++failure_index) {
        auto family = AliasFamily { width };
        prepare_before_operation(family, operation);
        const auto actual_before = snapshot(family);
        require(actual_before == before,
            "every injected iteration begins with the same masks and family values");

        bool failed { };
        arm_allocation_failure(failure_index);
        try {
            apply_operation(family, operation);
        } catch (const std::bad_alloc&) {
            failed = true;
        } catch (...) {
            clear_allocation_failure();
            throw;
        }
        clear_allocation_failure();

        require(failed == (failure_index < allocation_count),
            "each measured allocation fails once and the terminal index succeeds");
        const auto after_attempt = snapshot(family);
        require(after_attempt == before || after_attempt == expected,
            "failure never leaves partial force masks, current leaves, proxy, or container state");
        require(after_attempt.frames_clear && after_attempt.planes_match,
            "every failed or successful attempt closes frames and keeps current planes coherent");

        if (failed) {
            ++injected_failures;
            if (after_attempt == before) {
                ++unchanged_failures;
            } else {
                ++completed_failures;
            }
            apply_operation(family, operation);
            require(snapshot(family) == expected,
                "retry applies the complete requested force mask and family value");
        } else {
            successful_probe = true;
        }

        perform_follow_up(family, operation);
    }

    require(successful_probe && injected_failures == allocation_count
            && unchanged_failures + completed_failures == injected_failures,
        "the sweep injects every allocation and records coherent old/new outcomes");
    std::cout << "aggregate alias force/release sweep " << label
              << " width=" << width
              << " sites=" << allocation_count
              << " injected_failures=" << injected_failures
              << " unchanged_failures=" << unchanged_failures
              << " completed_failures=" << completed_failures
              << " successful_index=" << allocation_count << '\n';
}

void test_aggregate_alias_force_release_allocation_failures()
{
    for (const auto width : std::array<std::size_t, 2U> { 65U, 129U }) {
        sweep_operation(width, ForceOperation::whole_force, "whole-force");
        sweep_operation(width, ForceOperation::partial_force, "partial-force");
        sweep_operation(width, ForceOperation::whole_release, "whole-release");
        sweep_operation(width, ForceOperation::partial_release, "partial-release");
    }
}

} // namespace
} // namespace fsim::tests::runtime

int main()
{
    try {
        fsim::tests::runtime::test_aggregate_alias_force_release_allocation_failures();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "aggregate alias force/release failure test failed: "
                  << error.what() << '\n';
        fsim::tests::runtime::staging_failure_support::
            clear_allocation_failure();
        return 1;
    }
}
