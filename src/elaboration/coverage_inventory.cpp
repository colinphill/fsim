// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_inventory.hpp"

#include "fsim/elaboration/elaborator.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace fsim::elaboration {
namespace {

    using Error = CoverageInventoryError;
    using Validation = CoverageInventoryValidationResult;

    Validation reject(const Error error, const std::size_t index = 0U) noexcept
    {
        return { error, index };
    }

    bool point_less(
        const CoverageInventoryPoint& left,
        const CoverageInventoryPoint& right) noexcept
    {
        return std::tie(left.point.id.high, left.point.id.low,
                   left.point.metric, left.source_index, left.span.begin_offset,
                   left.span.end_offset)
            < std::tie(right.point.id.high, right.point.id.low,
                right.point.metric, right.source_index, right.span.begin_offset,
                right.span.end_offset);
    }

    Validation validate_sources_and_owners(
        const std::span<const CoverageInventorySource> sources,
        const std::span<const CoverageInventoryOwner> owners,
        const CoverageInventoryLimits limits) noexcept
    {
        if (sources.size() > limits.maximum_sources
            || owners.size() > limits.maximum_instances) {
            return reject(Error::ResourceLimit);
        }
        std::unordered_set<std::string_view> source_names;
        std::unordered_set<std::string> source_identities;
        try {
            source_names.reserve(sources.size());
            source_identities.reserve(sources.size());
            for (std::size_t index = 0U; index < sources.size(); ++index) {
                const auto& source = sources[index];
                if (source.source_name.empty()) {
                    return reject(Error::EmptySourceName, index);
                }
                if (!source_names.emplace(source.source_name).second) {
                    return reject(Error::DuplicateSourceName, index);
                }
                if (!frontend::is_code_coverage_source_identity_valid(
                        source.identity)) {
                    return reject(Error::InvalidSourceIdentity, index);
                }
                if (!source_identities.emplace(
                                          frontend::code_coverage_source_identity_hex(
                                              source.identity))
                        .second) {
                    return reject(Error::DuplicateSourceIdentity, index);
                }
            }
        } catch (const std::bad_alloc&) {
            return reject(Error::ResourceLimit);
        } catch (const std::length_error&) {
            return reject(Error::ResourceLimit);
        }
        try {
            std::unordered_set<std::string> instance_identities;
            instance_identities.reserve(owners.size());
            for (std::size_t index = 0U; index < owners.size(); ++index) {
                const auto& owner = owners[index];
                if (owner.specialization != index || owner.source.empty()) {
                    return reject(Error::InvalidOwner, index);
                }
                const auto identity = make_coverage_instance_identity(
                    { owner.instance, owner.language, owner.library, owner.unit,
                        owner.parameter_identities },
                    limits.instance_identity);
                if (!identity.ok()) {
                    return reject(Error::InvalidInstanceIdentity, index);
                }
                if (!instance_identities.emplace(
                                            coverage_instance_identity_hex(*identity.identity))
                        .second) {
                    return reject(Error::DuplicateInstanceIdentity, index);
                }
            }
        } catch (const std::bad_alloc&) {
            return reject(Error::ResourceLimit);
        } catch (const std::length_error&) {
            return reject(Error::ResourceLimit);
        }
        return { };
    }

    CoverageInstanceIdentityResult owner_identity(
        const CoverageInventoryOwner& owner,
        const CoverageInventoryLimits limits) noexcept
    {
        return make_coverage_instance_identity(
            { owner.instance, owner.language, owner.library, owner.unit,
                owner.parameter_identities },
            limits.instance_identity);
    }

    Validation validate_point(
        const CoverageInventoryPointDraft& point,
        const std::span<const CoverageInventorySource> sources,
        const CoverageInventoryOwner& owner,
        const CoverageInventoryLimits limits,
        const std::size_t index) noexcept
    {
        if (!runtime::is_code_coverage_identity_valid(point.id)) {
            return reject(Error::InvalidPointIdentity, index);
        }
        if (!runtime::is_code_coverage_point_metric(point.metric)) {
            return reject(Error::InvalidMetric, index);
        }
        if (point.source_index >= sources.size()) {
            return reject(Error::UnknownPointSource, index);
        }
        const auto& source_name = sources[point.source_index].source_name;
        if (source_name != owner.source
            && std::ranges::find(owner.source_dependencies, source_name)
                == owner.source_dependencies.end()) {
            return reject(Error::PointSourceOwnershipMismatch, index);
        }
        if (point.span.begin_offset >= point.span.end_offset
            || point.span.end_offset
                > sources[point.source_index].identity.content_bytes) {
            return reject(Error::InvalidPointSpan, index);
        }
        if (point.line == 0U || point.line > limits.maximum_line_number) {
            return reject(Error::InvalidLine, index);
        }
        return { };
    }

    CoverageInventoryPointDraft draft_from(
        const CoverageInventoryPoint& point) noexcept
    {
        return { point.point.id, point.point.metric, point.source_index,
            point.span, point.line };
    }

} // namespace

CoverageInventoryValidationResult validate_code_coverage_inventory(
    const CodeCoverageInventory& inventory,
    const std::span<const CoverageInventoryOwner> owners,
    const CoverageInventoryLimits limits) noexcept
{
    const auto base = validate_sources_and_owners(
        inventory.sources, owners, limits);
    if (!base.ok()) {
        return base;
    }
    if (inventory.instances.size() > limits.maximum_instances
        || inventory.instances.size() != owners.size()) {
        return reject(inventory.instances.size() > limits.maximum_instances
                ? Error::ResourceLimit
                : Error::MissingInstance);
    }

    std::size_t total_points = 0U;
    for (std::size_t instance_index = 0U;
        instance_index < inventory.instances.size(); ++instance_index) {
        const auto& instance = inventory.instances[instance_index];
        const auto& owner = owners[instance_index];
        if (instance.specialization != instance_index) {
            return reject(Error::NonCanonicalOrder, instance_index);
        }
        if (instance.specialization != owner.specialization
            || instance.instance != owner.instance
            || instance.language != owner.language) {
            return reject(Error::InstanceOwnerMismatch, instance_index);
        }
        const auto identity = owner_identity(owner, limits);
        if (!identity.ok() || instance.identity != *identity.identity) {
            return reject(Error::InvalidInstanceIdentity, instance_index);
        }
        if (instance.points.size() > limits.maximum_points - total_points) {
            return reject(Error::ResourceLimit, instance_index);
        }
        if (!std::ranges::is_sorted(instance.points,
                [](const CoverageInventoryPoint& left,
                    const CoverageInventoryPoint& right) {
                    return point_less(left, right);
                })) {
            return reject(Error::NonCanonicalOrder, instance_index);
        }
        for (std::size_t point_index = 0U;
            point_index < instance.points.size(); ++point_index) {
            const auto& point = instance.points[point_index];
            const auto checked = validate_point(
                draft_from(point), inventory.sources, owner, limits,
                point_index);
            if (!checked.ok()) {
                return checked;
            }
            if (point.point.counter.value != total_points) {
                return reject(Error::CounterOwnershipMismatch, point_index);
            }
            if (point_index != 0U
                && instance.points[point_index - 1U].point.id
                    == point.point.id) {
                return reject(Error::DuplicatePoint, point_index);
            }
            ++total_points;
        }
    }
    if (inventory.total_points != total_points) {
        return reject(Error::TotalPointMismatch);
    }
    return { };
}

CoverageInventoryBuildResult make_code_coverage_inventory(
    const std::span<const CoverageInventorySource> sources,
    const std::span<const CoverageInstanceInventoryDraft> instances,
    const std::span<const CoverageInventoryOwner> owners,
    const CoverageInventoryLimits limits) noexcept
{
    const auto fail = [](const Validation result) {
        return CoverageInventoryBuildResult {
            std::nullopt, result.error, result.index
        };
    };
    const auto base = validate_sources_and_owners(sources, owners, limits);
    if (!base.ok()) {
        return fail(base);
    }
    if (instances.size() > limits.maximum_instances) {
        return fail(reject(Error::ResourceLimit));
    }
    if (instances.size() != owners.size()) {
        return fail(reject(Error::MissingInstance));
    }

    try {
        // Index drafts by their dense owner without copying their point
        // containers. Only the final retained point vectors are materialized.
        std::vector<const CoverageInstanceInventoryDraft*> ordered(
            owners.size(), nullptr);
        for (std::size_t index = 0U; index < instances.size(); ++index) {
            const auto& instance = instances[index];
            if (instance.specialization >= owners.size()) {
                return fail(reject(Error::UnknownInstance, index));
            }
            if (ordered[instance.specialization] != nullptr) {
                return fail(reject(Error::DuplicateInstance, index));
            }
            ordered[instance.specialization] = &instance;
        }
        for (std::size_t index = 0U; index < ordered.size(); ++index) {
            if (ordered[index] == nullptr) {
                return fail(reject(Error::MissingInstance, index));
            }
        }

        CodeCoverageInventory inventory;
        inventory.sources.assign(sources.begin(), sources.end());
        inventory.instances.reserve(ordered.size());
        std::size_t total_points = 0U;
        for (const auto* draft : ordered) {
            if (draft->points.size() > limits.maximum_points - total_points) {
                return fail(reject(Error::ResourceLimit,
                    draft->specialization));
            }
            for (std::size_t index = 0U; index < draft->points.size(); ++index) {
                const auto checked = validate_point(
                    draft->points[index], sources,
                    owners[draft->specialization], limits, index);
                if (!checked.ok()) {
                    return fail(checked);
                }
            }

            const auto& owner = owners[draft->specialization];
            CoverageInstanceInventory instance;
            instance.specialization = owner.specialization;
            const auto identity = owner_identity(owner, limits);
            if (!identity.ok()) {
                return fail(reject(
                    Error::InvalidInstanceIdentity, draft->specialization));
            }
            instance.identity = *identity.identity;
            instance.instance = owner.instance;
            instance.language = owner.language;
            instance.points.reserve(draft->points.size());
            for (const auto& point : draft->points) {
                instance.points.push_back(CoverageInventoryPoint {
                    { point.id, point.metric, { } }, point.source_index,
                    point.span, point.line });
            }
            std::ranges::sort(instance.points,
                [](const CoverageInventoryPoint& left,
                    const CoverageInventoryPoint& right) {
                    return point_less(left, right);
                });
            for (std::size_t index = 1U;
                index < instance.points.size(); ++index) {
                if (instance.points[index - 1U].point.id
                    == instance.points[index].point.id) {
                    return fail(reject(Error::DuplicatePoint, index));
                }
            }
            for (auto& point : instance.points) {
                if (total_points
                    > std::numeric_limits<std::uint32_t>::max()) {
                    return fail(reject(Error::ResourceLimit,
                        draft->specialization));
                }
                point.point.counter.value
                    = static_cast<std::uint32_t>(total_points);
                ++total_points;
            }
            inventory.instances.push_back(std::move(instance));
        }
        inventory.total_points = total_points;
        const auto checked = validate_code_coverage_inventory(
            inventory, owners, limits);
        if (!checked.ok()) {
            return fail(checked);
        }
        return { std::move(inventory), Error::None, 0U };
    } catch (const std::bad_alloc&) {
        return fail(reject(Error::ResourceLimit));
    } catch (const std::length_error&) {
        return fail(reject(Error::ResourceLimit));
    }
}

const std::optional<CodeCoverageInventory>&
ElaboratedDesign::code_coverage_inventory() const noexcept
{
    return code_coverage_inventory_;
}

CoverageInventoryValidationResult
ElaboratedDesign::attach_code_coverage_inventory(
    const std::span<const CoverageInventorySource> sources,
    const std::span<const CoverageInstanceInventoryDraft> instances,
    const CoverageInventoryLimits limits) noexcept
{
    try {
        std::vector<CoverageInventoryOwner> owners;
        owners.reserve(specializations_.size());
        for (const auto& specialization : specializations_) {
            const auto& parameters
                = specialization.parameter_identity_values.empty()
                ? specialization.parameter_values
                : specialization.parameter_identity_values;
            owners.push_back(CoverageInventoryOwner {
                specialization.id, specialization.instance,
                specialization.language, specialization.source,
                specialization.source_dependencies, specialization.library,
                specialization.unit, parameters });
        }
        auto built = make_code_coverage_inventory(
            sources, instances, owners, limits);
        if (!built.ok()) {
            return { built.error, built.index };
        }
        code_coverage_inventory_ = std::move(*built.inventory);
        return { };
    } catch (const std::bad_alloc&) {
        return { Error::ResourceLimit, 0U };
    } catch (const std::length_error&) {
        return { Error::ResourceLimit, 0U };
    }
}

} // namespace fsim::elaboration
