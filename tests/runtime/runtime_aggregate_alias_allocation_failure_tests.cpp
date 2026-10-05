// SPDX-License-Identifier: Apache-2.0
#include "../../src/runtime/simir_internal.hpp"
#include "runtime_fused_staging_failure_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
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

constexpr std::size_t maximum_allocation_sites = 512U;

void require(const bool condition, const char* const message)
{
    if (!condition) {
        throw std::runtime_error { message };
    }
}

template<class Implementation>
bool wide_current_planes_match(
    const Implementation& impl,
    const SignalId signal,
    const std::size_t width,
    const Logic4 value)
{
    const auto word_count = (width + 63U) / 64U;
    const auto offset = static_cast<std::size_t>(
        impl.direct_wide_signal_offsets.at(signal));
    if (offset > impl.direct_wide_signal_aval.size()
        || word_count > impl.direct_wide_signal_aval.size() - offset
        || offset > impl.direct_wide_signal_bval.size()
        || word_count > impl.direct_wide_signal_bval.size() - offset) {
        return false;
    }
    for (std::size_t word = 0U; word < word_count; ++word) {
        auto valid_bits = std::numeric_limits<std::uint64_t>::max();
        const auto tail_bits = width % 64U;
        if (word + 1U == word_count && tail_bits != 0U) {
            valid_bits = (UINT64_C(1) << tail_bits) - 1U;
        }
        const auto expected_aval
            = value == Logic4::one ? valid_bits : UINT64_C(0);
        if (impl.direct_wide_signal_aval[offset + word] != expected_aval
            || impl.direct_wide_signal_bval[offset + word] != 0U) {
            return false;
        }
    }
    return true;
}

template<class Implementation>
bool wide_current_planes_match_value(
    const Implementation& impl,
    const SignalId signal,
    const PackedLogic4& value)
{
    if (value.width() == 0U || value.is_logic9()) {
        return false;
    }
    const auto expected_aval = value.aval_words();
    const auto expected_bval = value.bval_words();
    const auto word_count = (value.width() + 63U) / 64U;
    if (expected_aval.size() != word_count
        || expected_bval.size() != word_count) {
        return false;
    }
    const auto offset = static_cast<std::size_t>(
        impl.direct_wide_signal_offsets.at(signal));
    if (offset > impl.direct_wide_signal_aval.size()
        || word_count > impl.direct_wide_signal_aval.size() - offset
        || offset > impl.direct_wide_signal_bval.size()
        || word_count > impl.direct_wide_signal_bval.size() - offset) {
        return false;
    }

    const auto tail_bits = value.width() % 64U;
    auto tail_mask = std::numeric_limits<std::uint64_t>::max();
    if (tail_bits != 0U) {
        tail_mask = (UINT64_C(1) << tail_bits) - 1U;
    }
    for (std::size_t word = 0U; word < word_count; ++word) {
        if (impl.direct_wide_signal_aval[offset + word]
                != expected_aval[word]
            || impl.direct_wide_signal_bval[offset + word]
                != expected_bval[word]) {
            return false;
        }
    }
    return ((impl.direct_wide_signal_aval[offset + word_count - 1U]
                & ~tail_mask)
                == 0U)
        && ((impl.direct_wide_signal_bval[offset + word_count - 1U]
                & ~tail_mask)
                == 0U);
}

struct AliasFamily {
    Interpreter interpreter;
    std::size_t width;
    SignalId first { };
    SignalId second { };
    SignalId aggregate { };
    ContainerObjectId object { };
    const ContainerValue* retained_container { };

    explicit AliasFamily(const std::size_t element_width)
        : width(element_width)
    {
        const auto zeros = PackedLogic4(width, Logic4::zero);
        first = interpreter.add_signal({
            "words[1]", zeros, ResolutionKind::sv_wire });
        second = interpreter.add_signal({
            "words[0]", zeros, ResolutionKind::sv_wire });
        aggregate = interpreter.add_signal({
            "words", PackedLogic4(width * 2U, Logic4::zero),
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
            { object, aggregate, true, true });
    }

    void retain_container_reference()
    {
        retained_container = &interpreter.container_object_value(object);
    }
};

std::string four_state_boundary_pattern(
    const std::size_t width,
    const std::size_t phase)
{
    constexpr std::array<char, 4U> pattern { '0', '1', 'X', 'Z' };
    std::string symbols(width, '0');
    for (std::size_t bit = 0U; bit < width; ++bit) {
        auto symbol = pattern[(bit + phase) % pattern.size()];
        switch (bit % 64U) {
        case 62U:
            symbol = phase == 0U ? 'X' : 'Z';
            break;
        case 63U:
            symbol = phase == 0U ? 'Z' : 'X';
            break;
        case 0U:
            if (bit != 0U) {
                symbol = phase == 0U ? 'X' : 'Z';
            }
            break;
        default:
            break;
        }
        symbols[width - bit - 1U] = symbol;
    }
    return symbols;
}

void verify_four_state_alias_family(const std::size_t width)
{
    auto family = AliasFamily(width);
    family.interpreter.start();
    family.retain_container_reference();

    const auto first_symbols = four_state_boundary_pattern(width, 0U);
    const auto second_symbols = four_state_boundary_pattern(width, 1U);
    const auto first = PackedLogic4::from_msb_string(first_symbols);
    const auto second = PackedLogic4::from_msb_string(second_symbols);
    auto aggregate_symbols = first_symbols;
    aggregate_symbols.append(second_symbols);
    const auto aggregate
        = PackedLogic4::from_msb_string(aggregate_symbols);
    const auto zeros = PackedLogic4(width, Logic4::zero);
    const auto aggregate_zeros = PackedLogic4(width * 2U, Logic4::zero);

    for (std::size_t boundary = 64U; boundary < width; boundary += 64U) {
        require(first.get(boundary - 2U) == Logic4::x
                && first.get(boundary - 1U) == Logic4::z
                && first.get(boundary) == Logic4::x
                && second.get(boundary - 2U) == Logic4::z
                && second.get(boundary - 1U) == Logic4::x
                && second.get(boundary) == Logic4::z,
            "mixed four-state values straddle every tested word boundary");
    }

    family.interpreter.deposit_signal(family.aggregate, aggregate);
    auto& impl = OwnedDriverDemotionTestAccess::implementation(
        family.interpreter);
    require(wide_current_planes_match_value(impl, family.first, first)
            && wide_current_planes_match_value(impl, family.second, second),
        "published raw leaf A/B planes preserve four-state data and mask tail bits");
    require(family.interpreter.signal_value(family.first) == first
            && family.interpreter.signal_value(family.second) == second
            && family.interpreter.signal_value(family.aggregate) == aggregate,
        "aggregate alias publication preserves distinct four-state leaf data");
    require(family.interpreter.stored_signal_value(family.first) == first
            && family.interpreter.stored_signal_value(family.second) == second
            && family.interpreter.stored_signal_value(family.aggregate)
                == aggregate
            && impl.driven_values.at(family.first) == first
            && impl.driven_values.at(family.second) == second,
        "stored leaf backing retains X and Z independently");
    require(impl.signal_last_values.at(family.first) == zeros
            && impl.signal_last_values.at(family.second) == zeros
            && impl.aggregate_signal_last_value(family.aggregate)
                == aggregate_zeros,
        "leaf and aggregate LAST values retain the pre-write zero family");

    const auto& container
        = family.interpreter.container_object_value(family.object);
    require(family.retained_container != nullptr
            && family.retained_container->elements.size() == 2U
            && family.retained_container->elements[0U] == first
            && family.retained_container->elements[1U] == second
            && container.elements[0U] == first
            && container.elements[1U] == second,
        "retained container reads match both mixed four-state leaves");
}

void verify_family_state(
    AliasFamily& family,
    const PackedLogic4& current_element,
    const PackedLogic4& stored_element,
    const PackedLogic4& previous_element)
{
    const auto current_aggregate = PackedLogic4(
        family.width * 2U,
        current_element.get(0U));
    const auto stored_aggregate = PackedLogic4(
        family.width * 2U,
        stored_element.get(0U));
    const auto previous_aggregate = PackedLogic4(
        family.width * 2U,
        previous_element.get(0U));
    auto& impl = OwnedDriverDemotionTestAccess::implementation(
        family.interpreter);

    require(impl.aggregate_signal_batches.at(family.aggregate).depth == 0U,
        "aggregate batch depth returns to zero after a failed or completed write");
    require(impl.container_alias_write_batches.at(family.object).frames.empty(),
        "container alias write frames are cleared after a failed or completed write");

    require(family.interpreter.signal_value(family.first) == current_element
            && family.interpreter.signal_value(family.second) == current_element
            && family.interpreter.signal_value(family.aggregate)
                == current_aggregate,
        "all current leaf and proxy values match the requested family state");
    require(family.interpreter.stored_signal_value(family.first)
                == stored_element
            && family.interpreter.stored_signal_value(family.second)
                == stored_element
            && family.interpreter.stored_signal_value(family.aggregate)
                == stored_aggregate,
        "all stored leaf and proxy values match the requested family state");
    require(impl.driven_values.at(family.first) == stored_element
            && impl.driven_values.at(family.second) == stored_element
            && impl.aggregate_signal_stored_projection.at(family.aggregate)
                && *impl.aggregate_signal_stored_projection.at(family.aggregate)
                    == stored_aggregate
            && impl.aggregate_signal_current_projection.at(family.aggregate)
            && *impl.aggregate_signal_current_projection.at(family.aggregate)
                == current_aggregate
            && impl.aggregate_signal_stored_projection_revisions.at(
                   family.aggregate)
                == impl.container_aggregate_stored_revisions.at(family.object)
            && impl.aggregate_signal_current_projection_revisions.at(
                   family.aggregate)
                == impl.container_aggregate_current_revisions.at(family.object),
        "raw driver stores, aggregate projections, and family revisions stay coherent");
    require(wide_current_planes_match(
                impl, family.first, family.width, current_element.get(0U))
            && wide_current_planes_match(
                impl, family.second, family.width, current_element.get(0U)),
        "all raw wide current planes match the complete current family");
    const auto& container
        = family.interpreter.container_object_value(family.object);
    require(family.retained_container != nullptr
            && family.retained_container->elements.size() == 2U
            && family.retained_container->elements[0] == current_element
            && family.retained_container->elements[1] == current_element,
        "a retained aggregate ContainerValue reference matches the complete current family");
    require(container.elements.size() == 2U
            && container.elements[0] == current_element
            && container.elements[1] == current_element,
        "retained container reads agree with every current family member");
    require(impl.signal_last_values.at(family.first) == previous_element
            && impl.signal_last_values.at(family.second) == previous_element
            && impl.aggregate_signal_last_value(family.aggregate)
                == previous_aggregate,
        "leaf and proxy LAST values preserve the prior complete family");
}

std::size_t measure_successful_write_allocations(const std::size_t width)
{
    auto family = AliasFamily(width);
    family.interpreter.start();
    family.retain_container_reference();
    const auto ones = PackedLogic4(width * 2U, Logic4::one);

    begin_allocation_count();
    family.interpreter.deposit_signal(family.aggregate, ones);
    const auto allocation_count = end_allocation_count();
    require(allocation_count != 0U,
        "the successful wide whole-family write exercises allocations");
    require(allocation_count <= maximum_allocation_sites,
        "the allocation sweep remains within its bounded site cap");
    std::cout << "aggregate alias allocation sweep width=" << width
              << " cap=" << maximum_allocation_sites
              << " measured_sites=" << allocation_count
              << " status=starting\n";
    return allocation_count;
}

void sweep_whole_family_write(const std::size_t width)
{
    const auto allocation_count
        = measure_successful_write_allocations(width);
    const auto zeros = PackedLogic4(width, Logic4::zero);
    const auto ones = PackedLogic4(width, Logic4::one);
    const auto aggregate_zeros
        = PackedLogic4(width * 2U, Logic4::zero);
    const auto aggregate_ones
        = PackedLogic4(width * 2U, Logic4::one);

    std::size_t injected_failures { };
    std::size_t stored_only_failures { };
    bool successful_probe { };
    for (std::size_t failure_index = 0U;
        failure_index <= allocation_count;
        ++failure_index) {
        auto family = AliasFamily(width);
        family.interpreter.start();
        family.retain_container_reference();

        bool failed { };
        arm_allocation_failure(failure_index);
        try {
            family.interpreter.deposit_signal(
                family.aggregate, aggregate_ones);
        } catch (const std::bad_alloc&) {
            failed = true;
        } catch (...) {
            clear_allocation_failure();
            throw;
        }
        clear_allocation_failure();

        require(failed == (failure_index < allocation_count),
            "each counted allocation fails once and the terminal index succeeds");
        if (failed) {
            ++injected_failures;

            const auto current = family.interpreter.signal_value(family.first);
            const bool current_is_old = current == zeros;
            const bool current_is_new = current == ones;
            const auto stored = family.interpreter.stored_signal_value(
                family.first);
            const bool stored_is_old = stored == zeros;
            const bool stored_is_new = stored == ones;
            require((current_is_old || current_is_new)
                    && (stored_is_old || stored_is_new)
                    && (!current_is_new || stored_is_new),
                "failure leaves each phase wholly old or new in legal order");
            if (current_is_old && stored_is_new) {
                ++stored_only_failures;
            }
            require(family.interpreter.signal_value(family.second) == current
                    && family.interpreter.signal_value(family.aggregate)
                        == (current_is_old
                                ? aggregate_zeros
                                : aggregate_ones)
                    && family.interpreter.stored_signal_value(family.second)
                        == stored
                    && family.interpreter.stored_signal_value(family.aggregate)
                        == (stored_is_old
                                ? aggregate_zeros
                                : aggregate_ones),
                "failure never leaves a partial leaf, proxy, or stored family");
            verify_family_state(
                family, current, stored, zeros);

            family.interpreter.deposit_signal(
                family.aggregate, aggregate_ones);
        } else {
            successful_probe = true;
        }

        verify_family_state(family, ones, ones, zeros);
        family.interpreter.deposit_signal(
            family.aggregate, aggregate_zeros);
        verify_family_state(family, zeros, zeros, ones);
    }

    require(successful_probe && injected_failures == allocation_count
            && stored_only_failures != 0U,
        "the sweep observes every failure, store-only phase, and full success");
    std::cout << "aggregate alias allocation sweep width=" << width
              << " cap=" << maximum_allocation_sites
              << " sites=" << allocation_count
              << " injected_failures=" << injected_failures
              << " stored_only_failures=" << stored_only_failures
              << " successful_index=" << allocation_count << '\n';
}

struct FlatAlias {
    Interpreter interpreter { };
    std::size_t element_width { };
    PackedLogic4 zeros { 0U, Logic4::zero };
    PackedLogic4 ones { 0U, Logic4::zero };
    SignalId signal { };
    ContainerObjectId object { };
    const ContainerValue* retained_container { };
    const PackedLogic4* retained_elements { };

    explicit FlatAlias(const std::size_t width)
        : element_width(width)
        , zeros(width, Logic4::zero)
        , ones(width, Logic4::one)
    {
        signal = interpreter.add_signal({
            "flat_alias.backing",
            PackedLogic4(width * 2U, Logic4::zero) });
        ContainerType type;
        type.fixed = true;
        type.element_width = static_cast<std::uint32_t>(width);
        type.index_left = 1;
        type.index_right = 0;
        type.dimensions = { { 1, 0 } };
        object = interpreter.add_container_object({
            "flat_alias.array",
            ContainerValue { type, { zeros, zeros }, { } },
            std::nullopt });
        interpreter.add_container_signal_alias(
            { object, signal, true, true });
        interpreter.start();
        retained_container = &interpreter.container_object_value(object);
        retained_elements = retained_container->elements.data();
    }

    [[nodiscard]] PackedLogic4 whole(const Logic4 state) const
    {
        return PackedLogic4(element_width * 2U, state);
    }
};

struct LateFlatAliasCapture {
    Interpreter* interpreter { };
    SignalId signal { };
    ContainerObjectId object { };
    ContainerObjectId slice { };
    ContainerObjectId higher_object { };
    const ContainerValue* higher_object_value { };
    const PackedLogic4* higher_object_elements { };
    const ContainerValue* object_value { };
    const ContainerValue* slice_value { };
    const PackedLogic4* object_elements { };
    const PackedLogic4* slice_elements { };
    bool saw_prepublication_value { };
    bool allocation_failure_seen { };
    bool orphan_reservation_failure_seen { };
    bool failure_state_valid { true };
    bool failed { };
    bool complete { };

    void capture_references()
    {
        if (slice_value == nullptr) {
            const auto& value
                = interpreter->container_object_value(slice);
            slice_value = &value;
            slice_elements = value.elements.data();
            saw_prepublication_value
                = value.elements.size() == 1U
                && value.elements[0U].get(0U) == Logic4::zero;
        }
        if (object_value == nullptr) {
            const auto& value
                = interpreter->container_object_value(object);
            object_value = &value;
            object_elements = value.elements.data();
        }
        if (higher_object_value == nullptr) {
            const auto& value
                = interpreter->container_object_value(higher_object);
            higher_object_value = &value;
            higher_object_elements = value.elements.data();
        }
        complete = true;
    }

    static void trace(
        void* const context,
        const SchedulerTraceRecord& record) noexcept
    {
        auto& capture = *static_cast<LateFlatAliasCapture*>(context);
        if (record.kind != SchedulerTraceKind::signal_transaction
            || record.signal != capture.signal || capture.complete) {
            return;
        }
        try {
            capture.capture_references();
        } catch (const std::bad_alloc&) {
            capture.allocation_failure_seen = true;
            auto& impl = OwnedDriverDemotionTestAccess::implementation(
                *capture.interpreter);
            if (capture.higher_object
                    < impl.container_value_reference_exposed.size()
                && impl.container_value_reference_exposed.at(
                       capture.higher_object) != 0U) {
                const auto& value
                    = impl.container_objects.at(capture.higher_object)
                          .initial_value;
                capture.higher_object_value = &value;
                capture.higher_object_elements = value.elements.data();
            }
            bool object_pin_has_all_refresh_reservations { true };
            if (impl.container_value_reference_exposed.at(capture.object)
                != 0U) {
                for (auto* frame = impl.active_container_reference_refresh;
                    frame != nullptr; frame = frame->previous) {
                    if (!frame->needs_refresh
                        || !impl.container_value_reference_depends_on_signals(
                            capture.object, frame->signals)) {
                        continue;
                    }
                    const auto& refreshes = frame->prepared->flat_aliases;
                    object_pin_has_all_refresh_reservations
                        &= std::ranges::any_of(
                            refreshes,
                            [object = capture.object](const auto& refresh) {
                                return refresh.object == object;
                        });
                }
            } else {
                bool has_late_reservation { };
                for (auto* frame = impl.active_container_reference_refresh;
                    frame != nullptr; frame = frame->previous) {
                    if (frame->needs_refresh && frame->prepared != nullptr) {
                        has_late_reservation |= std::ranges::any_of(
                            frame->prepared->flat_aliases,
                            [object = capture.object](const auto& refresh) {
                                return refresh.object == object;
                        });
                    }
                }
                capture.orphan_reservation_failure_seen
                    |= has_late_reservation
                    && capture.object
                        < impl.container_value_reference_exposed.size()
                    && impl.container_value_reference_exposed.at(
                           capture.object) == 0U
                    && capture.higher_object
                        < impl.container_value_reference_exposed.size()
                    && impl.container_value_reference_exposed.at(
                           capture.higher_object) != 0U
                    && impl.container_value_reference_exposed.at(
                           capture.slice) == 0U
                    && capture.object_value == nullptr
                    && capture.slice_value == nullptr;
            }
            const bool slice_pin_has_returned_reference
                = capture.slice_value != nullptr
                || impl.container_value_reference_exposed.at(capture.slice)
                    == 0U;
            capture.failure_state_valid
                &= object_pin_has_all_refresh_reservations
                && slice_pin_has_returned_reference
                && impl.container_value_reference_exposure_in_progress.at(
                       capture.object) == 0U
                && impl.container_value_reference_exposure_in_progress.at(
                       capture.slice) == 0U;
        } catch (...) {
            capture.failed = true;
        }
    }
};

void verify_flat_alias_current(const FlatAlias& alias, const Logic4 state)
{
    const auto expected = alias.whole(state);
    const auto high = expected.extract_bits(
        alias.element_width, alias.element_width);
    const auto low = expected.extract_bits(0U, alias.element_width);
    require(alias.interpreter.signal_value(alias.signal) == expected
            && alias.retained_container != nullptr
            && alias.retained_container->elements.size() == 2U
            && alias.retained_container->elements[0] == high
            && alias.retained_container->elements[1] == low
            && alias.retained_container->elements.data()
                == alias.retained_elements,
        "retained flat alias storage matches the whole published signal in place");
}

std::size_t measure_flat_alias_allocations(const std::size_t width)
{
    FlatAlias alias(width);
    const auto ones = alias.whole(Logic4::one);
    begin_allocation_count();
    alias.interpreter.deposit_signal(alias.signal, ones);
    const auto allocations = end_allocation_count();
    require(allocations != 0U && allocations <= maximum_allocation_sites,
        "retained flat alias publication has a bounded measured allocation window");
    verify_flat_alias_current(alias, Logic4::one);
    return allocations;
}

void sweep_flat_alias_reference_refresh(const std::size_t width)
{
    const auto allocation_count = measure_flat_alias_allocations(width);
    std::size_t injected_failures { };
    bool successful_probe { };
    for (std::size_t failure_index = 0U;
        failure_index <= allocation_count;
        ++failure_index) {
        FlatAlias alias(width);
        const auto ones = alias.whole(Logic4::one);
        bool failed { };
        arm_allocation_failure(failure_index);
        try {
            alias.interpreter.deposit_signal(alias.signal, ones);
        } catch (const std::bad_alloc&) {
            failed = true;
        } catch (...) {
            clear_allocation_failure();
            throw;
        }
        clear_allocation_failure();

        require(failed == (failure_index < allocation_count),
            "the flat alias sweep injects each measured allocation cut");
        if (failed) {
            ++injected_failures;
            const auto current = alias.interpreter.signal_value(alias.signal);
            const bool old_current = current == alias.whole(Logic4::zero);
            const bool new_current = current == alias.whole(Logic4::one);
            require(old_current || new_current,
                "a failed flat alias update leaves one complete current value");
            verify_flat_alias_current(
                alias, old_current ? Logic4::zero : Logic4::one);
            alias.interpreter.deposit_signal(alias.signal, ones);
        } else {
            successful_probe = true;
        }

        verify_flat_alias_current(alias, Logic4::one);
        alias.interpreter.deposit_signal(
            alias.signal, alias.whole(Logic4::zero));
        verify_flat_alias_current(alias, Logic4::zero);
    }
    require(successful_probe && injected_failures == allocation_count,
        "every flat alias preparation allocation fails before or after a coherent publication");
    std::cout << "flat alias reference sweep width=" << width
              << " sites=" << allocation_count
              << " injected_failures=" << injected_failures << '\n';
}

struct LateFlatAlias {
    Interpreter interpreter { };
    std::size_t element_width { };
    PackedLogic4 zeros { 0U, Logic4::zero };
    PackedLogic4 ones { 0U, Logic4::one };
    SignalId signal { };
    ContainerObjectId object { };
    ContainerObjectId slice { };
    ContainerObjectId higher_object { };
    LateFlatAliasCapture capture;

    explicit LateFlatAlias(const std::size_t width)
        : element_width(width)
        , zeros(width, Logic4::zero)
        , ones(width, Logic4::one)
    {
        signal = interpreter.add_signal({
            "late_flat_alias.backing",
            PackedLogic4(width * 2U, Logic4::zero) });
        ContainerType type;
        type.fixed = true;
        type.element_width = static_cast<std::uint32_t>(width);
        type.index_left = 1;
        type.index_right = 0;
        type.dimensions = { { 1, 0 } };
        object = interpreter.add_container_object({
            "late_flat_alias.array",
            ContainerValue { type, { zeros, zeros }, { } },
            std::nullopt });
        interpreter.add_container_signal_alias(
            { object, signal, true, true });
        type.index_right = 1;
        type.dimensions = { { 1, 1 } };
        slice = interpreter.add_container_object({
            "late_flat_alias.slice",
            ContainerValue { type, { zeros }, { } },
            ContainerSliceAlias { object, 1, 1 } });
        type.index_right = 0;
        type.dimensions = { { 1, 0 } };
        higher_object = interpreter.add_container_object({
            "late_flat_alias.higher_id_array",
            ContainerValue { type, { zeros, zeros }, { } },
            std::nullopt });
        interpreter.add_container_signal_alias(
            { higher_object, signal, true, true });
        require(higher_object > slice && higher_object > object,
            "the pre-exposed flat alias has a higher object identifier");
        Process terminator;
        terminator.id = 0U;
        terminator.name = "late_flat_alias.terminator";
        terminator.operations = { Halt { } };
        (void)interpreter.add_process(std::move(terminator));
        interpreter.start();

        const auto& impl
            = OwnedDriverDemotionTestAccess::implementation(interpreter);
        require(impl.container_value_reference_exposed.at(object) == 0U
                && impl.container_value_reference_exposed.at(slice) == 0U
                && impl.container_value_reference_exposed.at(higher_object) == 0U,
            "all flat and slice references remain unexposed before the callback");

        capture.interpreter = &interpreter;
        capture.signal = signal;
        capture.object = object;
        capture.slice = slice;
        capture.higher_object = higher_object;
        interpreter.scheduler().set_trace_hook(
            &capture, &LateFlatAliasCapture::trace);
    }

    [[nodiscard]] PackedLogic4 whole(const Logic4 state) const
    {
        return PackedLogic4(element_width * 2U, state);
    }

    void clear_trace() noexcept
    {
        interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    }
};

void verify_late_flat_alias_current(
    const LateFlatAlias& alias, const Logic4 state)
{
    const auto expected = alias.whole(state);
    const auto high = expected.extract_bits(
        alias.element_width, alias.element_width);
    const auto low = expected.extract_bits(0U, alias.element_width);
    require(alias.capture.complete && !alias.capture.failed
            && alias.capture.failure_state_valid
            && alias.capture.object_value != nullptr
            && alias.capture.slice_value != nullptr
            && alias.capture.higher_object_value != nullptr
            && alias.capture.higher_object_value->elements.size() == 2U
            && alias.capture.higher_object_value->elements[0U] == high
            && alias.capture.higher_object_value->elements[1U] == low
            && alias.capture.higher_object_value->elements.data()
                == alias.capture.higher_object_elements
            && alias.capture.object_value->elements.size() == 2U
            && alias.capture.object_value->elements[0U] == high
            && alias.capture.object_value->elements[1U] == low
            && alias.capture.object_value->elements.data()
                == alias.capture.object_elements
            && alias.capture.slice_value->elements.size() == 1U
            && alias.capture.slice_value->elements[0U] == high
            && alias.capture.slice_value->elements.data()
                == alias.capture.slice_elements,
        "a trace-time first exposure retains in-place array and slice backing");
}

std::size_t measure_late_flat_alias_allocations(const std::size_t width)
{
    LateFlatAlias alias(width);
    const auto ones = alias.whole(Logic4::one);
    begin_allocation_count();
    alias.interpreter.deposit_signal(alias.signal, ones);
    const auto allocations = end_allocation_count();
    alias.clear_trace();
    require(alias.interpreter.run().status == RunStatus::completed,
        "the first-exposure allocation probe drains its queued terminator");
    require(allocations != 0U && allocations <= maximum_allocation_sites,
        "first retained exposure has a bounded measured allocation window");
    verify_late_flat_alias_current(alias, Logic4::one);
    require(alias.capture.saw_prepublication_value,
        "the successful first exposure observes the old current phase");
    return allocations;
}

void sweep_late_flat_alias_exposure(const std::size_t width)
{
    const auto allocation_count
        = measure_late_flat_alias_allocations(width);
    std::size_t injected_failures { };
    std::size_t callback_exposure_failures { };
    std::size_t orphan_reservation_failures { };
    bool successful_probe { };
    for (std::size_t failure_index = 0U;
        failure_index <= allocation_count;
        ++failure_index) {
        LateFlatAlias alias(width);
        const auto ones = alias.whole(Logic4::one);
        bool outer_failed { };
        arm_allocation_failure(failure_index);
        try {
            alias.interpreter.deposit_signal(alias.signal, ones);
        } catch (const std::bad_alloc&) {
            outer_failed = true;
        } catch (...) {
            clear_allocation_failure();
            alias.clear_trace();
            throw;
        }
        clear_allocation_failure();

        const bool callback_failed
            = alias.capture.allocation_failure_seen;
        require((outer_failed || callback_failed)
                == (failure_index < allocation_count),
            "each measured cut fails either outer preflight or trace-time pinning");
        if (outer_failed || callback_failed) {
            ++injected_failures;
        }
        if (callback_failed) {
            ++callback_exposure_failures;
        }
        if (alias.capture.orphan_reservation_failure_seen) {
            ++orphan_reservation_failures;
        }
        if (outer_failed) {
            const auto current
                = alias.interpreter.signal_value(alias.signal);
            const bool old_current
                = current == alias.whole(Logic4::zero);
            const bool new_current
                = current == alias.whole(Logic4::one);
            require(old_current || new_current,
                "an injected failure leaves one complete current value");
            if (old_current) {
                alias.interpreter.deposit_signal(alias.signal, ones);
            }
        } else if (!callback_failed) {
            successful_probe = true;
        }
        alias.clear_trace();
        if (alias.capture.orphan_reservation_failure_seen) {
            require(alias.capture.higher_object_value->elements.size() == 2U
                    && alias.capture.higher_object_value->elements[0U] == alias.ones
                    && alias.capture.higher_object_value->elements[1U] == alias.ones
                    && alias.capture.higher_object_value->elements.data()
                        == alias.capture.higher_object_elements,
                "publication skips the unused lower-ID reservation and refreshes the retained higher-ID alias");
        }
        if (!alias.capture.complete) {
            // A failed getter did not publish a reference. Retry after the
            // surrounding write completes; the first reference must observe
            // the new value, while the pre-exposed higher-ID alias was kept
            // coherent as the frame skipped the unused reservation.
            alias.capture.capture_references();
        }
        require(alias.interpreter.run().status == RunStatus::completed,
            "the first-exposure allocation case drains its queued terminator");

        verify_late_flat_alias_current(alias, Logic4::one);
        alias.interpreter.deposit_signal(
            alias.signal, alias.whole(Logic4::zero));
        verify_late_flat_alias_current(alias, Logic4::zero);
    }
    require(injected_failures == allocation_count
            && callback_exposure_failures != 0U
            && orphan_reservation_failures != 0U
            && successful_probe,
        "failed late exposure leaves an inert reservation and retries safely");
    std::cout << "late flat alias exposure sweep width=" << width
              << " sites=" << allocation_count
              << " injected_failures=" << injected_failures
              << " callback_exposure_failures="
              << callback_exposure_failures
              << " orphan_reservation_failures="
              << orphan_reservation_failures << '\n';
}

} // namespace

void test_aggregate_alias_allocation_failures()
{
    for (const auto width : std::array<std::size_t, 2U> { 65U, 129U }) {
        sweep_whole_family_write(width);
        verify_four_state_alias_family(width);
        sweep_flat_alias_reference_refresh(width);
        sweep_late_flat_alias_exposure(width);
    }
}

} // namespace fsim::tests::runtime

int main()
{
    try {
        fsim::tests::runtime::test_aggregate_alias_allocation_failures();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "aggregate alias allocation failure test failed: "
                  << error.what() << '\n';
        fsim::tests::runtime::staging_failure_support::
            clear_allocation_failure();
        return 1;
    }
}
