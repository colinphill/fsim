// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_memory_toggle.hpp"

#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <map>
#include <new>
#include <ranges>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace fsim::elaboration {
namespace {

    using Error = CoverageMemoryToggleError;
    using Kind = CoverageToggleExcludedContainerKind;

    struct PendingElement {
        runtime::CodeCoveragePointId source_point;
        runtime::CodeCoveragePointId point;
        runtime::CodeCoveragePointId exclusion;
        frontend::CodeCoverageLanguage language {
            frontend::CodeCoverageLanguage::SystemVerilog
        };
        std::string hierarchy_path;
        std::vector<std::int64_t> indices;
        std::optional<std::string> string_key;
        std::size_t first_bit { };
        std::size_t bit_count { };
    };

    void update_u64(support::Sha256& hash, const std::uint64_t value) noexcept
    {
        std::array<std::byte, 8U> bytes { };
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            const auto shift = static_cast<unsigned>(
                (bytes.size() - index - 1U) * 8U);
            bytes[index] = static_cast<std::byte>((value >> shift) & 0xffU);
        }
        hash.update(bytes);
    }

    void update_string(support::Sha256& hash, const std::string_view value) noexcept
    {
        update_u64(hash, value.size());
        hash.update(value);
    }

    std::uint64_t digest_word(
        const support::Sha256::Digest& digest, const std::size_t first) noexcept
    {
        std::uint64_t value { };
        for (std::size_t index = first; index < first + 8U; ++index) {
            value = (value << 8U) | digest[index];
        }
        return value;
    }

    runtime::CodeCoveragePointId element_identity(
        const runtime::CodeCoveragePointId exclusion,
        const std::span<const std::int64_t> indices,
        const std::optional<std::string>& string_key) noexcept
    {
        support::Sha256 hash;
        update_string(hash, kCoverageMemoryToggleSchema);
        update_u64(hash, exclusion.high);
        update_u64(hash, exclusion.low);
        update_u64(hash, indices.size());
        for (const auto index : indices) {
            update_u64(hash, static_cast<std::uint64_t>(index));
        }
        update_u64(hash, string_key.has_value() ? 1U : 0U);
        if (string_key) {
            update_string(hash, *string_key);
        }
        const auto digest = hash.finish();
        return { digest_word(digest, 0U), digest_word(digest, 8U) };
    }

    bool invalid_text(const std::string_view text) noexcept
    {
        return text.find('\0') != std::string_view::npos;
    }

    bool has_container_reason(const CoverageToggleExclusion& exclusion) noexcept
    {
        return std::ranges::find(exclusion.reasons,
                   CoverageToggleExclusionReason::Memory)
            != exclusion.reasons.end()
            || std::ranges::find(exclusion.reasons,
                   CoverageToggleExclusionReason::Array)
            != exclusion.reasons.end();
    }

    bool contains(const CoverageToggleExcludedDimension& dimension,
        const std::int64_t value) noexcept
    {
        const auto lower = std::min(dimension.left, dimension.right);
        const auto upper = std::max(dimension.left, dimension.right);
        return value >= lower && value <= upper;
    }

    std::optional<std::size_t> range_count(
        const CoverageMemoryToggleIndexRange range,
        const CoverageMemoryToggleLimits& limits) noexcept
    {
        const auto distance = range.first >= range.last
            ? static_cast<std::uint64_t>(range.first)
                - static_cast<std::uint64_t>(range.last)
            : static_cast<std::uint64_t>(range.last)
                - static_cast<std::uint64_t>(range.first);
        if (distance == std::numeric_limits<std::uint64_t>::max()
            || distance + 1U > limits.maximum_range_span
            || distance + 1U > std::numeric_limits<std::size_t>::max()) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(distance + 1U);
    }

    std::string numeric_path(const std::string_view base,
        const std::span<const std::int64_t> indices)
    {
        std::string path { base };
        for (const auto index : indices) {
            path += "[" + std::to_string(index) + "]";
        }
        return path;
    }

    std::string string_path(
        const std::string_view base, const std::string_view key)
    {
        return std::string { base } + "[#" + std::to_string(key.size()) + ":"
            + std::string { key } + "]";
    }

    bool pending_less(
        const PendingElement& left, const PendingElement& right) noexcept
    {
        return std::tie(left.hierarchy_path, left.first_bit, left.bit_count,
                   left.point.high, left.point.low)
            < std::tie(right.hierarchy_path, right.first_bit, right.bit_count,
                right.point.high, right.point.low);
    }

} // namespace

CoverageMemoryToggleResult make_coverage_memory_toggle_inventory(
    const CoverageToggleSelection& defaults,
    const std::span<const CoverageMemoryToggleRule> rules,
    const CoverageMemoryToggleLimits limits) noexcept
{
    CoverageMemoryToggleResult result;
    const auto reject = [&](const Error error, const std::size_t rule = 0U,
                            const std::size_t selector = 0U) {
        result.inventory.reset();
        result.error = error;
        result.rule_index = rule;
        result.selector_index = selector;
        return result;
    };

    try {
        if (!is_coverage_instance_identity_valid(defaults.instance_identity)
            || defaults.instance.empty() || invalid_text(defaults.instance)) {
            return reject(Error::InvalidSelectionIdentity);
        }
        if (rules.size() > limits.maximum_rules) {
            return reject(Error::ResourceLimit);
        }
        if (defaults.exclusions.size() > limits.maximum_exclusions) {
            return reject(Error::ResourceLimit);
        }

        std::map<std::string_view, const CoverageToggleExclusion*, std::less<>>
            exclusions;
        std::set<std::pair<std::uint64_t, std::uint64_t>> exclusion_ids;
        for (const auto& exclusion : defaults.exclusions) {
            if (!runtime::is_code_coverage_identity_valid(exclusion.id)
                || !runtime::is_code_coverage_identity_valid(
                    exclusion.source_point)
                || exclusion.instance_identity != defaults.instance_identity
                || exclusion.specialization != defaults.specialization) {
                return reject(Error::InvalidExclusionIdentity);
            }
            if (!exclusion_ids.emplace(exclusion.id.high, exclusion.id.low)
                    .second) {
                return reject(Error::DuplicateExclusionIdentity);
            }
            if (exclusion.hierarchy_path.empty()
                || invalid_text(exclusion.hierarchy_path)
                || !exclusions
                    .emplace(exclusion.hierarchy_path, &exclusion)
                    .second) {
                return reject(Error::DuplicateExclusionPath);
            }
        }

        std::vector<PendingElement> pending;
        std::set<std::string> rule_paths;
        std::set<std::tuple<std::uint64_t, std::uint64_t, std::size_t>> bits;
        std::map<std::pair<std::uint64_t, std::uint64_t>, std::string>
            element_ids;
        std::size_t selector_count = 0U;
        std::size_t key_count = 0U;

        for (std::size_t rule_index = 0U; rule_index < rules.size();
            ++rule_index) {
            const auto& rule = rules[rule_index];
            if (rule.hierarchy_path.empty()) {
                return reject(Error::EmptyRulePath, rule_index);
            }
            if (invalid_text(rule.hierarchy_path)
                || rule.hierarchy_path.size()
                    > limits.maximum_hierarchy_bytes) {
                return reject(rule.hierarchy_path.size()
                            > limits.maximum_hierarchy_bytes
                        ? Error::ResourceLimit
                        : Error::InvalidRulePath,
                    rule_index);
            }
            if (!rule_paths.emplace(rule.hierarchy_path).second) {
                return reject(Error::DuplicateRulePath, rule_index);
            }
            const auto found = exclusions.find(rule.hierarchy_path);
            if (found == exclusions.end()) {
                return reject(Error::UnknownRulePath, rule_index);
            }
            const auto& exclusion = *found->second;
            if (!has_container_reason(exclusion)
                || exclusion.shape.kind == Kind::None) {
                return reject(Error::ObjectNotExcludedContainer, rule_index);
            }
            if (exclusion.shape.element_width == 0U) {
                return reject(Error::UnsupportedElementType, rule_index);
            }
            if (rule.selectors.empty()) {
                return reject(Error::MissingSelector, rule_index);
            }
            if (rule.selectors.size()
                > limits.maximum_selectors - selector_count) {
                return reject(Error::ResourceLimit, rule_index);
            }
            selector_count += rule.selectors.size();

            for (std::size_t selector_index = 0U;
                selector_index < rule.selectors.size(); ++selector_index) {
                const auto& selector = rule.selectors[selector_index];
                if (selector.bit_count == 0U
                    || selector.first_bit >= exclusion.shape.element_width
                    || selector.bit_count
                        > exclusion.shape.element_width - selector.first_bit) {
                    return reject(Error::InvalidBitRange, rule_index,
                        selector_index);
                }

                const bool string_associative = exclusion.shape.kind
                        == Kind::AssociativeArray
                    && exclusion.shape.string_index;
                if (string_associative) {
                    if (!selector.dimensions.empty()
                        || selector.string_keys.empty()) {
                        return reject(Error::InvalidSelectorForm, rule_index,
                            selector_index);
                    }
                    if (selector.string_keys.size()
                        > limits.maximum_string_keys - key_count) {
                        return reject(Error::ResourceLimit, rule_index,
                            selector_index);
                    }
                    key_count += selector.string_keys.size();
                    for (const auto& key : selector.string_keys) {
                        if (key.empty()) {
                            return reject(Error::EmptyStringKey, rule_index,
                                selector_index);
                        }
                        if (invalid_text(key)) {
                            return reject(Error::InvalidStringKey, rule_index,
                                selector_index);
                        }
                        if (key.size() > limits.maximum_string_key_bytes) {
                            return reject(Error::ResourceLimit, rule_index,
                                selector_index);
                        }
                        const auto path = string_path(rule.hierarchy_path, key);
                        if (path.size() > limits.maximum_hierarchy_bytes
                            || pending.size() >= limits.maximum_elements) {
                            return reject(Error::ResourceLimit, rule_index,
                                selector_index);
                        }
                        const std::optional<std::string> retained_key { key };
                        const auto point = element_identity(exclusion.id,
                            std::span<const std::int64_t> { }, retained_key);
                        const auto canonical = path;
                        const auto [identity, inserted]
                            = element_ids.emplace(
                                std::pair { point.high, point.low }, canonical);
                        if (!runtime::is_code_coverage_identity_valid(point)
                            || (!inserted && identity->second != canonical)) {
                            return reject(Error::DuplicateElementIdentity,
                                rule_index, selector_index);
                        }
                        if (selector.bit_count
                            > limits.maximum_bits - bits.size()) {
                            return reject(Error::ResourceLimit, rule_index,
                                selector_index);
                        }
                        for (std::size_t bit = selector.first_bit;
                            bit < selector.first_bit + selector.bit_count;
                            ++bit) {
                            if (!bits.emplace(point.high, point.low, bit)
                                    .second) {
                                return reject(Error::DuplicateBitSelection,
                                    rule_index, selector_index);
                            }
                        }
                        pending.push_back(PendingElement {
                            exclusion.source_point, point, exclusion.id,
                            exclusion.language, std::move(path), { },
                            retained_key, selector.first_bit,
                            selector.bit_count });
                    }
                    continue;
                }

                if (!selector.string_keys.empty()) {
                    return reject(Error::InvalidSelectorForm, rule_index,
                        selector_index);
                }
                const auto required_dimensions
                    = exclusion.shape.kind == Kind::StaticArray
                        || exclusion.shape.kind == Kind::VhdlArray
                    ? exclusion.shape.dimensions.size()
                    : 1U;
                if (required_dimensions == 0U
                    || selector.dimensions.size() != required_dimensions
                    || selector.dimensions.size()
                        > limits.maximum_dimensions) {
                    return reject(Error::InvalidDimensionCount, rule_index,
                        selector_index);
                }
                if ((exclusion.shape.kind == Kind::StaticArray
                        || exclusion.shape.kind == Kind::VhdlArray)
                    && std::ranges::any_of(exclusion.shape.dimensions,
                        [](const auto& dimension) {
                            return !dimension.concrete;
                        })) {
                    return reject(Error::UnspecializedDimensions, rule_index,
                        selector_index);
                }

                std::size_t element_count = 1U;
                for (std::size_t dimension_index = 0U;
                    dimension_index < selector.dimensions.size();
                    ++dimension_index) {
                    const auto range = selector.dimensions[dimension_index];
                    const auto count = range_count(range, limits);
                    if (!count || *count > limits.maximum_elements / element_count) {
                        return reject(Error::ResourceLimit, rule_index,
                            selector_index);
                    }
                    element_count *= *count;
                    if (exclusion.shape.kind == Kind::StaticArray
                        || exclusion.shape.kind == Kind::VhdlArray) {
                        const auto& declared
                            = exclusion.shape.dimensions[dimension_index];
                        if (!contains(declared, range.first)
                            || !contains(declared, range.last)) {
                            return reject(Error::IndexOutOfRange, rule_index,
                                selector_index);
                        }
                    } else if ((exclusion.shape.kind == Kind::DynamicArray
                                   || exclusion.shape.kind == Kind::Queue)
                        && (range.first < 0 || range.last < 0)) {
                        return reject(Error::IndexOutOfRange, rule_index,
                            selector_index);
                    }
                }
                if (element_count > limits.maximum_elements - pending.size()
                    || element_count
                        > (limits.maximum_bits - bits.size())
                            / selector.bit_count) {
                    return reject(Error::ResourceLimit, rule_index,
                        selector_index);
                }

                std::vector<std::int64_t> coordinates;
                coordinates.reserve(selector.dimensions.size());
                std::function<Error(std::size_t)> expand;
                expand = [&](const std::size_t dimension_index) -> Error {
                    if (dimension_index == selector.dimensions.size()) {
                        auto path = numeric_path(
                            rule.hierarchy_path, coordinates);
                        if (path.size() > limits.maximum_hierarchy_bytes) {
                            return Error::ResourceLimit;
                        }
                        const auto point = element_identity(
                            exclusion.id, coordinates, std::nullopt);
                        const auto canonical = path;
                        const auto [identity, inserted]
                            = element_ids.emplace(
                                std::pair { point.high, point.low }, canonical);
                        if (!runtime::is_code_coverage_identity_valid(point)
                            || (!inserted && identity->second != canonical)) {
                            return Error::DuplicateElementIdentity;
                        }
                        for (std::size_t bit = selector.first_bit;
                            bit < selector.first_bit + selector.bit_count;
                            ++bit) {
                            if (!bits.emplace(point.high, point.low, bit)
                                    .second) {
                                return Error::DuplicateBitSelection;
                            }
                        }
                        pending.push_back(PendingElement {
                            exclusion.source_point, point, exclusion.id,
                            exclusion.language, std::move(path), coordinates,
                            std::nullopt, selector.first_bit,
                            selector.bit_count });
                        return Error::None;
                    }
                    const auto range = selector.dimensions[dimension_index];
                    auto value = range.first;
                    while (true) {
                        coordinates.push_back(value);
                        const auto error = expand(dimension_index + 1U);
                        coordinates.pop_back();
                        if (error != Error::None) {
                            return error;
                        }
                        if (value == range.last) {
                            break;
                        }
                        value += value < range.last ? 1 : -1;
                    }
                    return Error::None;
                };
                if (const auto error = expand(0U); error != Error::None) {
                    return reject(error, rule_index, selector_index);
                }
            }
        }

        std::ranges::sort(pending, pending_less);
        CoverageMemoryToggleInventory inventory;
        inventory.instance_identity = defaults.instance_identity;
        inventory.specialization = defaults.specialization;
        inventory.instance = defaults.instance;
        inventory.elements.reserve(pending.size());
        inventory.outcomes.reserve(bits.size());
        for (auto& selected : pending) {
            const auto first_outcome = inventory.outcomes.size();
            for (std::size_t bit = selected.first_bit;
                bit < selected.first_bit + selected.bit_count; ++bit) {
                inventory.outcomes.push_back(
                    runtime::CoverageToggleOutcome { selected.point, bit });
            }
            inventory.elements.push_back(CoverageMemoryToggleElement {
                selected.source_point, selected.point, selected.exclusion,
                defaults.instance_identity, defaults.specialization,
                selected.language, std::move(selected.hierarchy_path),
                std::move(selected.indices), std::move(selected.string_key),
                selected.first_bit, selected.bit_count, first_outcome });
        }
        result.inventory = std::move(inventory);
        result.error = Error::None;
        result.rule_index = 0U;
        result.selector_index = 0U;
        return result;
    } catch (const std::bad_alloc&) {
        return reject(Error::ResourceLimit, result.rule_index,
            result.selector_index);
    } catch (const std::length_error&) {
        return reject(Error::ResourceLimit, result.rule_index,
            result.selector_index);
    }
}

} // namespace fsim::elaboration
