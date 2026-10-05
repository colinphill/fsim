// SPDX-License-Identifier: Apache-2.0
#include "../../src/compiler/llvm_jit_region_activation_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fsim::compiler::llvm_detail::activation_register_word_layout_matches;
using fsim::compiler::llvm_detail::activation_wide_backing_matches;
using fsim::compiler::llvm_detail::RegionKernelActivationBacking;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

struct Fixture final {
    std::vector<std::uint32_t> signal_widths { 1U, 64U, 65U, 129U };
    std::vector<fsim::runtime::simir::ValueKind> signal_value_kinds {
        fsim::runtime::simir::ValueKind::logic4,
        fsim::runtime::simir::ValueKind::logic4,
        fsim::runtime::simir::ValueKind::logic4,
        fsim::runtime::simir::ValueKind::logic4 };
    std::vector<fsim::runtime::simir::ValueKind>
        mismatched_signal_value_kinds;
    std::vector<std::uint32_t> direct_update_signals { 0U, 2U, 3U };
    std::vector<std::uint32_t> wide_signal_offsets { 0U, 1U, 2U, 4U };
    std::vector<std::uint64_t> direct_signal_aval { 4U, 5U, 6U, 7U };
    std::vector<std::uint64_t> direct_signal_bval { 0U, 0U, 0U, 0U };
    std::vector<std::uint64_t> wide_signal_aval = std::vector<std::uint64_t>(7U, 0U);
    std::vector<std::uint64_t> wide_signal_bval = std::vector<std::uint64_t>(7U, 0U);
    std::vector<std::size_t> update_wide_offsets { std::numeric_limits<std::size_t>::max(), 0U,
        2U };
    std::vector<std::uint64_t> update_wide_aval = std::vector<std::uint64_t>(5U, 0U);
    std::vector<std::uint64_t> update_wide_bval = std::vector<std::uint64_t>(5U, 0U);
    std::vector<std::uint64_t> update_wide_mask = std::vector<std::uint64_t>(5U, 0U);
    std::vector<fsim_jit_update_slot_v2> update_slots = std::vector<fsim_jit_update_slot_v2>(3U);
    fsim_jit_runtime_instance_v2 runtime { };
    RegionKernelActivationBacking backing { };

    Fixture()
    {
        update_slots[0U].width = 1U;
        update_slots[0U].word_count = 1U;
        update_slots[1U].width = 65U;
        update_slots[1U].word_count = 2U;
        update_slots[1U].wide_aval = update_wide_aval.data();
        update_slots[1U].wide_bval = update_wide_bval.data();
        update_slots[1U].wide_mask = update_wide_mask.data();
        update_slots[2U].width = 129U;
        update_slots[2U].word_count = 3U;
        update_slots[2U].wide_aval = update_wide_aval.data() + 2U;
        update_slots[2U].wide_bval = update_wide_bval.data() + 2U;
        update_slots[2U].wide_mask = update_wide_mask.data() + 2U;

        backing.signal_widths = signal_widths;
        backing.signal_value_kinds = signal_value_kinds;
        backing.direct_update_signals = direct_update_signals;
        backing.direct_signal_aval = direct_signal_aval;
        backing.direct_signal_bval = direct_signal_bval;
        backing.direct_wide_signal_offsets = wide_signal_offsets;
        backing.direct_wide_signal_aval = wide_signal_aval;
        backing.direct_wide_signal_bval = wide_signal_bval;
        backing.direct_update_slots = update_slots;
        backing.direct_update_wide_word_offsets = update_wide_offsets;
        backing.direct_update_wide_aval = update_wide_aval;
        backing.direct_update_wide_bval = update_wide_bval;
        backing.direct_update_wide_mask = update_wide_mask;
        sync_runtime();
    }

    void sync_runtime() noexcept
    {
        runtime.direct_wide_signal_aval = backing.direct_wide_signal_aval.data();
        runtime.direct_wide_signal_bval = backing.direct_wide_signal_bval.data();
        runtime.direct_wide_signal_offsets = backing.direct_wide_signal_offsets.data();
        runtime.direct_wide_signal_offset_count
                = static_cast<std::uint32_t>(backing.direct_wide_signal_offsets.size());
        runtime.direct_wide_word_count
                = static_cast<std::uint32_t>(backing.direct_wide_signal_aval.size());
        runtime.direct_update_slots = backing.direct_update_slots.data();
        runtime.direct_update_slot_count
                = static_cast<std::uint32_t>(backing.direct_update_slots.size());
    }

    void use_narrow_only_backing()
    {
        signal_widths = { 1U, 64U };
        signal_value_kinds.assign(2U,
            fsim::runtime::simir::ValueKind::logic4);
        direct_update_signals = { 0U, 1U };
        direct_signal_aval = { 4U, 5U };
        direct_signal_bval = { 0U, 0U };
        wide_signal_offsets.clear();
        wide_signal_aval.clear();
        wide_signal_bval.clear();
        update_wide_offsets.assign(2U, std::numeric_limits<std::size_t>::max());
        update_wide_aval.clear();
        update_wide_bval.clear();
        update_wide_mask.clear();
        update_slots.assign(2U, fsim_jit_update_slot_v2 { });
        update_slots[0U].width = 1U;
        update_slots[0U].word_count = 1U;
        update_slots[1U].width = 64U;
        update_slots[1U].word_count = 1U;

        backing.signal_widths = signal_widths;
        backing.signal_value_kinds = signal_value_kinds;
        backing.direct_update_signals = direct_update_signals;
        backing.direct_signal_aval = direct_signal_aval;
        backing.direct_signal_bval = direct_signal_bval;
        backing.direct_wide_signal_offsets = wide_signal_offsets;
        backing.direct_wide_signal_aval = wide_signal_aval;
        backing.direct_wide_signal_bval = wide_signal_bval;
        backing.direct_update_slots = update_slots;
        backing.direct_update_wide_word_offsets = update_wide_offsets;
        backing.direct_update_wide_aval = update_wide_aval;
        backing.direct_update_wide_bval = update_wide_bval;
        backing.direct_update_wide_mask = update_wide_mask;
        sync_runtime();
    }

    [[nodiscard]] bool matches() const noexcept
    {
        return activation_wide_backing_matches(
                runtime, signal_widths, signal_value_kinds,
                direct_update_signals, backing);
    }
};

template <typename Mutator>
void expect_rejected(const std::string_view description, Mutator&& mutate)
{
    Fixture fixture;
    mutate(fixture);
    require(!fixture.matches(), description);
}

void test_valid_mixed_width_backing()
{
    const Fixture fixture;
    require(fixture.matches(), "mixed narrow and wide Logic4 backing has exact contiguous offsets");
}

void test_narrow_only_empty_wide_backing()
{
    Fixture fixture;
    fixture.use_narrow_only_backing();
    require(fixture.matches(), "all-narrow kernels accept empty wide-plane vectors and offsets");
}

void test_register_word_layout()
{
    const std::vector<std::uint32_t> widths { 0U, 1U, 64U, 65U, 129U };
    const std::vector<std::uint32_t> offsets { 0U, 0U, 1U, 2U, 4U };
    const std::vector<std::uint32_t> gap_offsets { 0U, 0U, 1U, 3U, 4U };
    const std::vector<std::uint32_t> short_offsets { 0U, 1U, 2U, 3U };
    require(activation_register_word_layout_matches(widths, offsets, 7U),
            "wide register offsets form an exact prefix including zero-width slots");
    require(!activation_register_word_layout_matches(widths, gap_offsets, 7U),
            "register offset gaps are rejected");
    require(!activation_register_word_layout_matches(widths, offsets, 6U),
            "register word capacity mismatch is rejected");
    require(!activation_register_word_layout_matches(widths, short_offsets, 7U),
            "register width and offset counts must match");

    const std::vector<std::uint32_t> maximum_width { std::numeric_limits<std::uint32_t>::max() };
    const std::vector<std::uint32_t> maximum_offset { 0U };
    require(activation_register_word_layout_matches(maximum_width, maximum_offset, 67108864U),
            "maximum ABI width uses overflow-safe word rounding");
}

void test_signal_plane_identity_and_capacity()
{
    expect_rejected("signal kind backing must match its authenticated layout",
            [](Fixture& fixture) {
                fixture.mismatched_signal_value_kinds
                    = fixture.signal_value_kinds;
                fixture.mismatched_signal_value_kinds[0U]
                    = fsim::runtime::simir::ValueKind::logic9;
                fixture.backing.signal_value_kinds
                    = fixture.mismatched_signal_value_kinds;
            });
    expect_rejected(
            "wide signals cannot omit their offset and plane backing", [](Fixture& fixture) {
                fixture.wide_signal_offsets.clear();
                fixture.wide_signal_aval.clear();
                fixture.wide_signal_bval.clear();
                fixture.backing.direct_wide_signal_offsets = fixture.wide_signal_offsets;
                fixture.backing.direct_wide_signal_aval = fixture.wide_signal_aval;
                fixture.backing.direct_wide_signal_bval = fixture.wide_signal_bval;
                fixture.sync_runtime();
            });
    expect_rejected("signal offset gaps are rejected",
            [](Fixture& fixture) { fixture.wide_signal_offsets[2U] = 3U; });
    expect_rejected("signal plane capacity mismatch is rejected", [](Fixture& fixture) {
        fixture.backing.direct_wide_signal_aval
                = { fixture.wide_signal_aval.data(), fixture.wide_signal_aval.size() - 1U };
    });
    expect_rejected("signal offset map capacity mismatch is rejected", [](Fixture& fixture) {
        fixture.backing.direct_wide_signal_offsets
                = { fixture.wide_signal_offsets.data(), fixture.wide_signal_offsets.size() - 1U };
    });
    expect_rejected("runtime signal plane identity mismatch is rejected", [](Fixture& fixture) {
        fixture.runtime.direct_wide_signal_bval = fixture.backing.direct_wide_signal_aval.data();
    });
    expect_rejected("runtime wide word bound mismatch is rejected",
            [](Fixture& fixture) { --fixture.runtime.direct_wide_word_count; });
    expect_rejected("aliased signal planes are rejected", [](Fixture& fixture) {
        fixture.backing.direct_wide_signal_bval = fixture.backing.direct_wide_signal_aval;
        fixture.sync_runtime();
    });
    expect_rejected("non-null Logic9 planes are rejected", [](Fixture& fixture) {
        fixture.runtime.direct_wide_signal_logic9_plane2 = fixture.wide_signal_aval.data();
    });
}

void test_update_slot_partition_and_capacity()
{
    expect_rejected("update offsets must be exact wide-only prefixes",
            [](Fixture& fixture) { fixture.update_wide_offsets[2U] = 3U; });
    expect_rejected("update planes must share their exact extent", [](Fixture& fixture) {
        fixture.backing.direct_update_wide_mask
                = { fixture.update_wide_mask.data(), fixture.update_wide_mask.size() - 1U };
    });
    expect_rejected("every slot has one wide-offset entry", [](Fixture& fixture) {
        fixture.backing.direct_update_wide_word_offsets
                = { fixture.update_wide_offsets.data(), fixture.update_wide_offsets.size() - 1U };
    });
    expect_rejected("runtime slot count must match its backing",
            [](Fixture& fixture) { --fixture.runtime.direct_update_slot_count; });
    expect_rejected("wide slot word count must be exact",
            [](Fixture& fixture) { fixture.update_slots[2U].word_count = 2U; });
    expect_rejected("wide slot width must match its mapped signal",
            [](Fixture& fixture) { --fixture.update_slots[2U].width; });
    expect_rejected("wide slot pointers must name their own region", [](Fixture& fixture) {
        fixture.update_slots[2U].wide_aval = fixture.update_wide_aval.data() + 1U;
    });
    expect_rejected("narrow slots use the no-wide-region sentinel",
            [](Fixture& fixture) { fixture.update_wide_offsets[0U] = 0U; });
    expect_rejected("narrow slots cannot expose wide plane pointers", [](Fixture& fixture) {
        fixture.update_slots[0U].wide_mask = fixture.update_wide_mask.data();
    });
    expect_rejected("wide update planes cannot overlap", [](Fixture& fixture) {
        fixture.backing.direct_update_wide_bval = fixture.backing.direct_update_wide_aval;
        fixture.update_slots[1U].wide_bval = fixture.backing.direct_update_wide_bval.data();
        fixture.update_slots[2U].wide_bval = fixture.backing.direct_update_wide_bval.data() + 2U;
    });
    expect_rejected("wide signal and frame planes cannot overlap", [](Fixture& fixture) {
        fixture.backing.register_aval = { fixture.wide_signal_aval.data(), 1U };
    });
    expect_rejected("slot signal IDs must be in range", [](Fixture& fixture) {
        fixture.direct_update_signals[2U]
                = static_cast<std::uint32_t>(fixture.signal_widths.size());
    });
}

} // namespace

int main()
{
    try {
        test_valid_mixed_width_backing();
        test_narrow_only_empty_wide_backing();
        test_register_word_layout();
        test_signal_plane_identity_and_capacity();
        test_update_slot_partition_and_capacity();
        std::cout << "region activation wide backing tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "region activation wide backing test failure: " << error.what() << '\n';
        return 1;
    }
}
