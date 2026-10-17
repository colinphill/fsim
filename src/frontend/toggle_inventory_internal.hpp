// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_instance_identity.hpp"
#include "fsim/frontend/design.hpp"
#include "fsim/frontend/source.hpp"
#include "fsim/support/identity128.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

// Object collection shared by the Verilog and VHDL toggle inventories.
namespace fsim::frontend::toggle_inventory_detail {

inline void update_string(
    support::Sha256& hash, const std::string_view value) noexcept
{
    support::sha256_update_u64_be(hash, value.size());
    hash.update(value);
}

[[nodiscard]] inline bool invalid_text(const std::string_view text) noexcept
{
    return text.find('\0') != std::string_view::npos;
}

template <typename Kind>
struct PendingObject {
    runtime::CodeCoveragePointId source_point;
    runtime::CodeCoveragePointId point;
    std::string hierarchy_path;
    Kind kind { };
    std::size_t source_index { };
    CodeCoverageSourceSpan span;
    std::uint64_t line { };
    std::size_t width { };
};

// Checks an inventory owner and its sources, then validates each toggle
// object of the unit and assigns its source and per-instance point
// identities. `Error` names the language's inventory errors.
template <typename Error, typename Kind, typename Limits>
class ToggleObjectCollector {
public:
    ToggleObjectCollector(const elaboration::CoverageInventoryOwner& owner,
        const std::span<const elaboration::VerilogCoverageSource> sources,
        const Limits& limits, const std::string_view schema,
        const CodeCoverageLanguage language,
        bool (*const is_toggle_type)(const Type&) noexcept)
        : owner_ { owner }
        , sources_ { sources }
        , limits_ { limits }
        , schema_ { schema }
        , language_ { language }
        , is_toggle_type_ { is_toggle_type }
    {
    }

    // Checks that the owner names this unit and that the sources are
    // distinct and valid.
    [[nodiscard]] Error begin(const std::string_view unit_identity,
        const std::string_view unit_library,
        const std::size_t declaration_count)
    {
        if (owner_.instance.empty() || owner_.source.empty()
            || owner_.library.empty() || owner_.unit.empty()
            || owner_.unit != unit_identity || owner_.library != unit_library
            || invalid_text(owner_.instance)) {
            return Error::InstanceOwnerMismatch;
        }
        const auto instance = elaboration::make_coverage_instance_identity(
            { owner_.instance, owner_.language, owner_.library, owner_.unit,
                owner_.parameter_identities },
            limits_.instance_identity);
        if (!instance.ok()) {
            return Error::InvalidInstanceIdentity;
        }
        instance_ = *instance.identity;
        if (sources_.size() > limits_.maximum_sources) {
            return Error::ResourceLimit;
        }
        std::set<std::string> source_identities;
        for (std::size_t index = 0U; index < sources_.size(); ++index) {
            const auto& source = sources_[index];
            if (source.source_name.empty()) {
                return Error::EmptySourceName;
            }
            if (!is_code_coverage_source_identity_valid(source.identity)) {
                return Error::InvalidSourceIdentity;
            }
            if (!source_by_name_.emplace(source.source_name, index).second) {
                return Error::DuplicateSourceName;
            }
            if (!source_identities
                     .emplace(code_coverage_source_identity_hex(
                         source.identity))
                     .second) {
                return Error::DuplicateSourceIdentity;
            }
        }
        if (declaration_count > limits_.maximum_objects) {
            return Error::ResourceLimit;
        }
        pending_.reserve(declaration_count);
        return Error::None;
    }

    // Counts a declaration that cannot be a toggle object.
    void skip() noexcept { ++visited_; }

    // Adds a declaration when its type is toggle-coverable.
    [[nodiscard]] Error append(const std::string_view name, const Type& type,
        const SourceSpan& source_span, const Kind kind)
    {
        const auto index = visited_++;
        if (!is_toggle_type_(type)) {
            return Error::None;
        }
        object_index_ = index;
        if (name.empty()) {
            return Error::EmptyObjectName;
        }
        if (invalid_text(name)
            || name.size() > limits_.maximum_object_name_bytes) {
            return name.size() > limits_.maximum_object_name_bytes
                ? Error::ResourceLimit
                : Error::InvalidObjectName;
        }
        std::string hierarchy_path;
        if (name.size() > limits_.maximum_hierarchy_bytes
            || owner_.instance.size()
                > limits_.maximum_hierarchy_bytes - name.size()
            || owner_.instance.size() + name.size()
                >= limits_.maximum_hierarchy_bytes) {
            return Error::ResourceLimit;
        }
        hierarchy_path.reserve(owner_.instance.size() + name.size() + 1U);
        hierarchy_path.append(owner_.instance);
        hierarchy_path.push_back('.');
        hierarchy_path.append(name);
        if (!hierarchy_paths_.emplace(hierarchy_path).second) {
            return Error::DuplicateObjectPath;
        }

        const auto physical_name = physical_source(source_span);
        const auto source = source_by_name_.find(physical_name);
        if (source == source_by_name_.end()) {
            return Error::UnknownObjectSource;
        }
        if (physical_name != owner_.source
            && std::ranges::find(owner_.source_dependencies, physical_name)
                == owner_.source_dependencies.end()) {
            return Error::ObjectSourceOwnershipMismatch;
        }
        const CodeCoverageSourceSpan span {
            static_cast<std::uint64_t>(source_span.begin.offset),
            static_cast<std::uint64_t>(source_span.end.offset),
        };
        const auto source_point = make_code_coverage_point_identity(
            sources_[source->second].identity, language_,
            CodeCoverageConstructKind::ToggleObject, span);
        if (!source_point.ok()) {
            return Error::InvalidObjectSpan;
        }
        if (source_span.begin.line == 0U
            || source_span.begin.line > limits_.maximum_line_number) {
            return Error::InvalidObjectLine;
        }
        const auto width = type.width();
        if (!width) {
            return Error::UnspecializedObjectWidth;
        }
        if (*width == 0U || *width > limits_.maximum_object_width
            || *width > std::numeric_limits<std::size_t>::max()) {
            return *width > limits_.maximum_object_width
                ? Error::ResourceLimit
                : Error::InvalidObjectWidth;
        }
        const auto width_value = static_cast<std::size_t>(*width);
        if (width_value > limits_.maximum_bits - total_bits_) {
            return Error::ResourceLimit;
        }
        total_bits_ += width_value;
        const auto point = instance_point_identity(
            *source_point.identity, kind, hierarchy_path);
        if (!runtime::is_code_coverage_identity_valid(point)
            || !point_ids_.emplace(point.high, point.low).second) {
            return Error::DuplicateObjectIdentity;
        }
        pending_.push_back(PendingObject<Kind> { *source_point.identity,
            point, std::move(hierarchy_path), kind, source->second, span,
            static_cast<std::uint64_t>(source_span.begin.line),
            width_value });
        object_index_ = 0U;
        return Error::None;
    }

    // The collected objects, ordered by hierarchy path and kind.
    [[nodiscard]] std::vector<PendingObject<Kind>>& sorted_objects()
    {
        std::ranges::sort(pending_, [](const PendingObject<Kind>& left,
                                        const PendingObject<Kind>& right) {
            return std::tie(left.hierarchy_path, left.kind,
                       left.source_point.high, left.source_point.low)
                < std::tie(right.hierarchy_path, right.kind,
                    right.source_point.high, right.source_point.low);
        });
        return pending_;
    }

    [[nodiscard]] elaboration::CoverageInstanceIdentity instance()
        const noexcept
    {
        return instance_;
    }
    [[nodiscard]] std::size_t total_bits() const noexcept
    {
        return total_bits_;
    }
    // The index of the declaration that the last failed append rejected.
    [[nodiscard]] std::size_t object_index() const noexcept
    {
        return object_index_;
    }

private:
    [[nodiscard]] runtime::CodeCoveragePointId instance_point_identity(
        const runtime::CodeCoveragePointId source_point, const Kind kind,
        const std::string_view hierarchy_path) const noexcept
    {
        support::Sha256 hash;
        update_string(hash, schema_);
        support::sha256_update_u64_be(hash, source_point.high);
        support::sha256_update_u64_be(hash, source_point.low);
        support::sha256_update_u64_be(hash, instance_.high);
        support::sha256_update_u64_be(hash, instance_.low);
        support::sha256_update_u64_be(hash, static_cast<std::uint8_t>(kind));
        update_string(hash, hierarchy_path);
        const auto digest = hash.finish();
        return { support::sha256_digest_word_be(digest, 0U),
            support::sha256_digest_word_be(digest, 8U) };
    }

    const elaboration::CoverageInventoryOwner& owner_;
    std::span<const elaboration::VerilogCoverageSource> sources_;
    const Limits& limits_;
    std::string_view schema_;
    CodeCoverageLanguage language_;
    bool (*is_toggle_type_)(const Type&) noexcept;
    elaboration::CoverageInstanceIdentity instance_ { };
    std::map<std::string_view, std::size_t, std::less<>> source_by_name_;
    std::vector<PendingObject<Kind>> pending_;
    std::set<std::string> hierarchy_paths_;
    std::set<std::pair<std::uint64_t, std::uint64_t>> point_ids_;
    std::size_t total_bits_ { };
    std::size_t visited_ { };
    std::size_t object_index_ { };
};

} // namespace fsim::frontend::toggle_inventory_detail
