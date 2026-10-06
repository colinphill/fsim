// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <vector>

namespace fsim::compiler::frontier_planner_detail {

template<typename BodyRange, typename MemberTemplateRange,
    typename ShapeCountRange>
void report_member_template_census(const BodyRange& bodies,
    const MemberTemplateRange& member_templates,
    const ShapeCountRange& shape_counts)
{
    if (std::getenv("FSIM_PROFILE_LLVM_MODULES") == nullptr
        || member_templates.size() != bodies.size()) {
        return;
    }

    std::size_t operation_occurrences { };
    std::size_t distinct_template_operations { };
    std::size_t outlined_members { };
    std::size_t binding_words { };
    std::vector<std::uint8_t> counted(shape_counts.size());
    for (std::size_t member = 0U; member < bodies.size(); ++member) {
        const auto& description = member_templates[member];
        operation_occurrences += bodies[member].operations.size();
        if (counted[description.shape_index] == 0U) {
            counted[description.shape_index] = 1U;
            distinct_template_operations
                += bodies[member].operations.size();
        }
        if (shape_counts[description.shape_index] > 1U) {
            ++outlined_members;
            binding_words += 1U + description.ordered_inputs.size()
                + 4U * description.ordered_sites.size();
        }
    }

    std::fprintf(stderr,
        "[fsim frontier-member-template-census] "
        "members=%zu planned_operation_occurrences=%zu "
        "distinct_template_operations=%zu template_shapes=%zu "
        "outlined_members=%zu direct_members=%zu binding_words=%zu "
        "retained_planner_operations=expanded\n",
        bodies.size(), operation_occurrences, distinct_template_operations,
        shape_counts.size(), outlined_members, bodies.size() - outlined_members,
        binding_words);
}

template<typename BodyRange>
void report_planner_storage(std::string_view event, void* plan_identity,
    const std::uint32_t layout_member_count, const BodyRange& bodies) noexcept
{
    if (std::getenv("FSIM_PROFILE_LLVM_MODULES") == nullptr) {
        return;
    }

    std::size_t operation_occurrences { };
    std::size_t operation_vector_capacity_bytes { };
    const auto maximum = std::numeric_limits<std::size_t>::max();
    for (const auto& body : bodies) {
        if (body.operations.size() > maximum - operation_occurrences) {
            operation_occurrences = maximum;
        } else {
            operation_occurrences += body.operations.size();
        }
        const auto capacity = body.operations.capacity();
        const auto element_size = sizeof(body.operations[std::size_t { }]);
        const auto body_bytes = capacity > maximum / element_size
            ? maximum
            : capacity * element_size;
        if (body_bytes > maximum - operation_vector_capacity_bytes) {
            operation_vector_capacity_bytes = maximum;
        } else {
            operation_vector_capacity_bytes += body_bytes;
        }
    }

    std::fprintf(stderr,
        "[fsim frontier-planner-storage] event=%.*s plan=%p "
        "layout_members=%u member_bodies=%zu "
        "operation_occurrences=%zu "
        "operation_vector_capacity_bytes_lower_bound=%zu "
        "nested_payload_capacity_included=0\n",
        static_cast<int>(event.size()), event.data(), plan_identity,
        static_cast<unsigned>(layout_member_count), bodies.size(),
        operation_occurrences, operation_vector_capacity_bytes);
}

} // namespace fsim::compiler::frontier_planner_detail
