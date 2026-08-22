// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_memory_toggle.hpp"
#include "fsim/frontend/coverage_source_identity.hpp"
#include "fsim/frontend/parser.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using ContainerKind
    = fsim::elaboration::CoverageToggleExcludedContainerKind;

constexpr std::string_view kParsedSource = R"(
module mem;
  logic [7:0] memory [3:2][0:1];
endmodule
)";

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::span<const std::byte> bytes(const std::string_view text)
{
    return std::as_bytes(std::span { text.data(), text.size() });
}

std::filesystem::path checkout_root()
{
#if defined(_WIN32)
    return "C:/fsim-memory-toggle";
#else
    return "/fsim-memory-toggle";
#endif
}

fsim::elaboration::CoverageToggleExclusion exclusion(std::string path,
    const std::uint64_t ordinal, const ContainerKind kind,
    std::vector<fsim::elaboration::CoverageToggleExcludedDimension> dimensions,
    const std::size_t element_width, const bool string_index = false)
{
    using namespace fsim;
    elaboration::CoverageToggleExclusion result;
    result.source_point = { 0x100U, ordinal };
    result.id = { 0x200U, ordinal };
    result.instance_identity = { 0x300U, 0x400U };
    result.specialization = 7U;
    result.hierarchy_path = std::move(path);
    result.reasons = { elaboration::CoverageToggleExclusionReason::Array };
    result.shape.kind = kind;
    result.shape.dimensions = std::move(dimensions);
    result.shape.element_width = element_width;
    result.shape.string_index = string_index;
    return result;
}

fsim::elaboration::CoverageToggleSelection defaults()
{
    using namespace fsim::elaboration;
    CoverageToggleSelection selection;
    selection.instance_identity = { 0x300U, 0x400U };
    selection.specialization = 7U;
    selection.instance = "top.u";
    selection.exclusions.push_back(exclusion("top.u.memory", 1U,
        ContainerKind::StaticArray,
        { { 3, 2, true }, { 0, 1, true } }, 8U));
    selection.exclusions.push_back(exclusion("top.u.dynamic", 2U,
        ContainerKind::DynamicArray, { }, 2U));
    selection.exclusions.push_back(exclusion("top.u.queue", 3U,
        ContainerKind::Queue, { }, 4U));
    selection.exclusions.push_back(exclusion("top.u.assoc", 4U,
        ContainerKind::AssociativeArray, { }, 4U, true));
    selection.exclusions.push_back(exclusion("top.u.vhdl_memory", 5U,
        ContainerKind::VhdlArray, { { 7, 4, true } }, 1U));
    return selection;
}

fsim::elaboration::CoverageMemoryToggleSelector numeric_selector(
    std::vector<fsim::elaboration::CoverageMemoryToggleIndexRange> dimensions,
    const std::size_t first_bit, const std::size_t bit_count)
{
    fsim::elaboration::CoverageMemoryToggleSelector selector;
    selector.dimensions = std::move(dimensions);
    selector.first_bit = first_bit;
    selector.bit_count = bit_count;
    return selector;
}

fsim::elaboration::CoverageMemoryToggleSelector string_selector(
    std::vector<std::string> keys, const std::size_t first_bit,
    const std::size_t bit_count)
{
    fsim::elaboration::CoverageMemoryToggleSelector selector;
    selector.string_keys = std::move(keys);
    selector.first_bit = first_bit;
    selector.bit_count = bit_count;
    return selector;
}

fsim::elaboration::CoverageMemoryToggleRule rule(std::string path,
    std::vector<fsim::elaboration::CoverageMemoryToggleSelector> selectors)
{
    return { std::move(path), std::move(selectors) };
}

void test_static_ranges_bits_and_identity()
{
    using namespace fsim;
    const auto selection = defaults();
    const std::array rules {
        rule("top.u.memory", { numeric_selector({ { 3, 2 }, { 0, 1 } }, 2U, 3U) })
    };
    const auto built = elaboration::make_coverage_memory_toggle_inventory(
        selection, rules);
    require(built.ok() && built.inventory->elements.size() == 4U
            && built.inventory->outcomes.size() == 12U,
        "an explicit two-dimensional range and bit slice must expand exactly");
    require(built.inventory->elements.front().hierarchy_path
                == "top.u.memory[2][0]"
            && built.inventory->elements.back().hierarchy_path
                == "top.u.memory[3][1]",
        "expanded memory elements must use canonical lexical path order");
    std::vector<runtime::CodeCoveragePointId> points;
    for (const auto& element : built.inventory->elements) {
        require(element.source_point == runtime::CodeCoveragePointId { 0x100U, 1U }
                && element.exclusion
                    == runtime::CodeCoveragePointId { 0x200U, 1U }
                && element.bit_count == 3U
                && element.first_bit == 2U
                && element.first_outcome + element.bit_count
                    <= built.inventory->outcomes.size(),
            "each selected element must retain exact source, exclusion, and outcome ownership");
        points.push_back(element.point);
        for (std::size_t bit = 0U; bit < element.bit_count; ++bit) {
            const auto& outcome
                = built.inventory->outcomes[element.first_outcome + bit];
            require(outcome.point == element.point
                    && outcome.bit_index == element.first_bit + bit,
                "selected bits must map densely to Change 7 outcomes");
        }
    }
    std::ranges::sort(points, [](const auto& left, const auto& right) {
        return std::pair { left.high, left.low }
        < std::pair { right.high, right.low };
    });
    require(std::ranges::adjacent_find(points) == points.end(),
        "each selected memory element must own a distinct point identity");
}

void test_parsed_static_memory_selection()
{
    using namespace fsim;
    const auto root = checkout_root();
    const auto path = root / "rtl/mem.sv";
    auto identity = frontend::make_code_coverage_source_identity(
        root, path, bytes(kParsedSource));
    require(identity.ok(), "parsed memory source identity must be valid");
    elaboration::VerilogCoverageSource source {
        path.generic_string(), std::move(*identity.identity)
    };
    const auto parsed = frontend::parse_text(
        source.source_name, kParsedSource,
        frontend::Language::SystemVerilog2017);
    require(parsed.ok() && parsed.design.units.size() == 1U,
        "independently authored static-memory source must parse");
    const elaboration::CoverageInventoryOwner owner { 0U, "root",
        frontend::Language::SystemVerilog2017, source.source_name, { },
        "work", "sv:work.mem", { } };
    const auto selected
        = elaboration::make_default_coverage_toggle_selection(
            parsed.design.units.front(), parsed.design.units.front().ports,
            owner, std::span { &source, 1U });
    require(selected.ok() && selected.selection->exclusions.size() == 1U,
        "parsed static memory must produce one explicit default exclusion");
    const auto& shape = selected.selection->exclusions.front().shape;
    require(shape.kind == ContainerKind::StaticArray
            && shape.dimensions
                == std::vector<elaboration::CoverageToggleExcludedDimension> {
                    { 3, 2, true }, { 0, 1, true } }
            && shape.element_width == 8U,
        "post-parse static memory shape must retain exact dimensions and packed element width");
    const auto explicit_rule = rule("root.memory",
        { numeric_selector({ { 3, 2 }, { 0, 1 } }, 4U, 2U) });
    const auto inventory
        = elaboration::make_coverage_memory_toggle_inventory(
            *selected.selection, std::span { &explicit_rule, 1U });
    require(inventory.ok() && inventory.inventory->elements.size() == 4U
            && inventory.inventory->outcomes.size() == 8U,
        "parsed static memory must expand only its explicit element and bit ranges");
}

void test_dynamic_associative_vhdl_and_order()
{
    using namespace fsim;
    const auto selection = defaults();
    std::vector rules {
        rule("top.u.dynamic", { numeric_selector({ { 1, 3 } }, 0U, 2U) }),
        rule("top.u.queue", { numeric_selector({ { 0, 0 } }, 2U, 2U) }),
        rule("top.u.assoc", { string_selector({ "beta", "alpha" }, 1U, 2U) }),
        rule("top.u.vhdl_memory",
            { numeric_selector({ { 6, 5 } }, 0U, 1U) }),
    };
    const auto first = elaboration::make_coverage_memory_toggle_inventory(
        selection, rules);
    require(first.ok() && first.inventory->elements.size() == 8U
            && first.inventory->outcomes.size() == 14U,
        "dynamic, associative, and VHDL explicit selections must compose");
    require(std::ranges::any_of(first.inventory->elements,
                [](const auto& element) {
                    return element.string_key
                        == std::optional<std::string> { "alpha" }
                    && element.hierarchy_path
                        == "top.u.assoc[#5:alpha]";
                }),
        "string associative keys must remain exact and collision-safe");
    std::ranges::reverse(rules);
    std::ranges::reverse(rules[1].selectors.front().string_keys);
    const auto reordered
        = elaboration::make_coverage_memory_toggle_inventory(selection, rules);
    require(reordered.ok() && reordered.inventory == first.inventory,
        "rule and key declaration order must not affect the canonical inventory");
}

void test_no_implicit_whole_container_and_failures()
{
    using namespace fsim;
    using Error = elaboration::CoverageMemoryToggleError;
    auto selection = defaults();
    const auto invoke = [&](const elaboration::CoverageMemoryToggleRule& input,
                            const elaboration::CoverageMemoryToggleLimits limits = { }) {
        return elaboration::make_coverage_memory_toggle_inventory(selection,
            std::span { &input, 1U }, limits);
    };

    require(invoke(rule("top.u.memory", { })).error
                == Error::MissingSelector
            && invoke(rule("top.u.memory",
                          { numeric_selector({ }, 0U, 1U) }))
                    .error
                == Error::InvalidDimensionCount,
        "no empty selector may silently enable a whole container");
    require(invoke(rule("top.u.missing",
                       { numeric_selector({ { 0, 0 } }, 0U, 1U) }))
                    .error
                == Error::UnknownRulePath
            && invoke(rule("top.u.memory",
                          { numeric_selector({ { 4, 4 }, { 0, 0 } }, 0U, 1U) }))
                    .error
                == Error::IndexOutOfRange
            && invoke(rule("top.u.dynamic",
                          { numeric_selector({ { -1, 0 } }, 0U, 1U) }))
                    .error
                == Error::IndexOutOfRange,
        "unknown objects and invalid static or runtime indices must be rejected");
    require(invoke(rule("top.u.memory",
                       { numeric_selector({ { 3, 3 }, { 0, 0 } }, 7U, 2U) }))
                    .error
                == Error::InvalidBitRange
            && invoke(rule("top.u.assoc",
                          { string_selector({ "" }, 0U, 1U) }))
                    .error
                == Error::EmptyStringKey,
        "bit slices and associative keys must be explicit and valid");

    const auto overlapping = rule("top.u.memory",
        { numeric_selector({ { 3, 3 }, { 0, 0 } }, 0U, 2U),
            numeric_selector({ { 3, 3 }, { 0, 0 } }, 1U, 2U) });
    require(invoke(overlapping).error == Error::DuplicateBitSelection,
        "overlapping selectors must never alias one scored bit");

    auto unspecialized = selection;
    unspecialized.exclusions.front().shape.dimensions.front().concrete = false;
    const auto unspecialized_rule = rule("top.u.memory",
        { numeric_selector({ { 3, 3 }, { 0, 0 } }, 0U, 1U) });
    require(elaboration::make_coverage_memory_toggle_inventory(
                unspecialized, std::span { &unspecialized_rule, 1U })
                .error
            == Error::UnspecializedDimensions,
        "static selection must reject unresolved elaboration-time bounds");

    auto unsupported = selection;
    unsupported.exclusions.front().shape.element_width = 0U;
    require(elaboration::make_coverage_memory_toggle_inventory(
                unsupported, std::span { &unspecialized_rule, 1U })
                .error
            == Error::UnsupportedElementType,
        "nonbinary element types must not manufacture toggle bins");
}

void test_resource_limits_and_transactionality()
{
    using namespace fsim;
    using Error = elaboration::CoverageMemoryToggleError;
    const auto selection = defaults();
    const auto wide = rule("top.u.dynamic",
        { numeric_selector({ { 0, 3 } }, 0U, 2U) });
    const auto invoke = [&](elaboration::CoverageMemoryToggleLimits limits) {
        return elaboration::make_coverage_memory_toggle_inventory(selection,
            std::span { &wide, 1U }, limits);
    };
    auto range = elaboration::CoverageMemoryToggleLimits { };
    range.maximum_range_span = 3U;
    auto elements = elaboration::CoverageMemoryToggleLimits { };
    elements.maximum_elements = 3U;
    auto bits = elaboration::CoverageMemoryToggleLimits { };
    bits.maximum_bits = 7U;
    auto selectors = elaboration::CoverageMemoryToggleLimits { };
    selectors.maximum_selectors = 0U;
    auto exclusions = elaboration::CoverageMemoryToggleLimits { };
    exclusions.maximum_exclusions = 0U;
    require(invoke(range).error == Error::ResourceLimit
            && invoke(elements).error == Error::ResourceLimit
            && invoke(bits).error == Error::ResourceLimit
            && invoke(selectors).error == Error::ResourceLimit
            && invoke(exclusions).error == Error::ResourceLimit,
        "exclusion, range, element, bit, and selector ceilings must reject before publication");
}

} // namespace

int main()
{
    test_static_ranges_bits_and_identity();
    test_parsed_static_memory_selection();
    test_dynamic_associative_vhdl_and_order();
    test_no_implicit_whole_container_and_failures();
    test_resource_limits_and_transactionality();
    std::cout << "Coverage memory toggle tests passed\n";
}
