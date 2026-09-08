// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/vhdl_toggle_inventory.hpp"

#include "fsim/frontend/source.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
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

    using Error = VhdlToggleInventoryError;

    struct PendingObject {
        runtime::CodeCoveragePointId source_point;
        runtime::CodeCoveragePointId point;
        std::string hierarchy_path;
        VhdlToggleObjectKind kind { VhdlToggleObjectKind::Signal };
        std::size_t source_index { };
        frontend::CodeCoverageSourceSpan span;
        std::uint64_t line { };
        std::size_t width { };
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

    runtime::CodeCoveragePointId instance_point_identity(
        const runtime::CodeCoveragePointId source_point,
        const CoverageInstanceIdentity instance,
        const VhdlToggleObjectKind kind,
        const std::string_view hierarchy_path) noexcept
    {
        support::Sha256 hash;
        update_string(hash, kVhdlToggleInventorySchema);
        update_u64(hash, source_point.high);
        update_u64(hash, source_point.low);
        update_u64(hash, instance.high);
        update_u64(hash, instance.low);
        update_u64(hash, static_cast<std::uint8_t>(kind));
        update_string(hash, hierarchy_path);
        const auto digest = hash.finish();
        return { digest_word(digest, 0U), digest_word(digest, 8U) };
    }

    bool valid_standard(const frontend::VhdlStandard standard) noexcept
    {
        switch (standard) {
        case frontend::VhdlStandard::Vhdl1987:
        case frontend::VhdlStandard::Vhdl1993:
        case frontend::VhdlStandard::Vhdl2000:
        case frontend::VhdlStandard::Vhdl2002:
        case frontend::VhdlStandard::Vhdl2008:
        case frontend::VhdlStandard::Vhdl2019:
            return true;
        }
        return false;
    }

    std::string vhdl_unit_identity(const frontend::DesignUnit& architecture)
    {
        const auto library = architecture.library.empty()
            ? std::string_view { "work" }
            : std::string_view { architecture.library };
        return "vhdl:" + std::string { library } + "."
            + architecture.primary_name + "(" + architecture.name + ")";
    }

    bool invalid_text(const std::string_view text) noexcept
    {
        return text.find('\0') != std::string_view::npos;
    }

    bool directly_packed_vhdl_vector(const frontend::Type& type) noexcept
    {
        if (!type.vhdl_array || type.vhdl_array->dimensions.size() != 1U
            || type.vhdl_array->element_types.size() != 1U
            || type.vhdl_array->unconstrained
            || !type.vhdl_array->dimensions.front().range
            || type.vhdl_array->dimensions.front().null) {
            return false;
        }
        const auto& element = type.vhdl_array->element_types.front();
        return !element.vhdl_array && element.packed_members.empty()
            && !element.vhdl_access && !element.vhdl_file
            && !element.vhdl_protected && !element.vhdl_physical
            && element.width() == 1U
            && (element.domain == frontend::ValueDomain::Bit2
                || element.domain == frontend::ValueDomain::Logic9);
    }

    bool object_less(const PendingObject& left, const PendingObject& right) noexcept
    {
        return std::tie(left.hierarchy_path, left.kind,
                   left.source_point.high, left.source_point.low)
            < std::tie(right.hierarchy_path, right.kind,
                right.source_point.high, right.source_point.low);
    }

} // namespace

bool is_vhdl_toggle_type(const frontend::Type& type) noexcept
{
    if (type.systemverilog_container
        || type.systemverilog_scalar
            != frontend::SystemVerilogScalarKind::None
        || !type.systemverilog_class_declaration.empty()
        || type.systemverilog_virtual_interface
        || !type.systemverilog_interface_type.empty()
        || type.vhdl_access || type.vhdl_file || type.vhdl_protected
        || type.vhdl_physical || !type.packed_members.empty()) {
        return false;
    }
    if (type.vhdl_array) {
        return directly_packed_vhdl_vector(type);
    }
    switch (type.domain) {
    case frontend::ValueDomain::Bit2:
    case frontend::ValueDomain::Logic9:
    case frontend::ValueDomain::Boolean:
    case frontend::ValueDomain::Integer:
        return true;
    case frontend::ValueDomain::Logic4:
    case frontend::ValueDomain::String:
    case frontend::ValueDomain::Unknown:
        return false;
    }
    return false;
}

VhdlToggleInventoryResult make_vhdl_toggle_inventory(
    const frontend::DesignUnit& architecture,
    const std::span<const frontend::SignalDeclaration> ports,
    const CoverageInventoryOwner& owner,
    const std::span<const VerilogCoverageSource> sources,
    const VhdlToggleInventoryLimits limits) noexcept
{
    VhdlToggleInventoryResult result;
    const auto reject = [&](const Error error,
                            const std::size_t object_index = 0U) {
        result.inventory.reset();
        result.error = error;
        result.object_index = object_index;
        return result;
    };

    try {
        if (architecture.language != frontend::Language::Vhdl2008
            || owner.language != frontend::Language::Vhdl2008) {
            return reject(Error::InvalidLanguage);
        }
        if (!valid_standard(architecture.vhdl_standard)) {
            return reject(Error::InvalidStandard);
        }
        if (architecture.kind != frontend::UnitKind::VhdlArchitecture
            || architecture.name.empty()
            || architecture.primary_name.empty()) {
            return reject(Error::InvalidUnitKind);
        }
        const auto unit_library = architecture.library.empty()
            ? std::string_view { "work" }
            : std::string_view { architecture.library };
        if (owner.instance.empty() || owner.source.empty()
            || owner.library.empty() || owner.unit.empty()
            || owner.unit != vhdl_unit_identity(architecture)
            || owner.library != unit_library
            || invalid_text(owner.instance)) {
            return reject(Error::InstanceOwnerMismatch);
        }
        const auto instance = make_coverage_instance_identity(
            { owner.instance, owner.language, owner.library, owner.unit,
                owner.parameter_identities },
            limits.instance_identity);
        if (!instance.ok()) {
            return reject(Error::InvalidInstanceIdentity);
        }
        if (sources.size() > limits.maximum_sources) {
            return reject(Error::ResourceLimit);
        }

        std::map<std::string_view, std::size_t, std::less<>> source_by_name;
        std::set<std::string> source_identities;
        for (std::size_t index = 0U; index < sources.size(); ++index) {
            const auto& source = sources[index];
            if (source.source_name.empty()) {
                return reject(Error::EmptySourceName);
            }
            if (!frontend::is_code_coverage_source_identity_valid(
                    source.identity)) {
                return reject(Error::InvalidSourceIdentity);
            }
            if (!source_by_name.emplace(source.source_name, index).second) {
                return reject(Error::DuplicateSourceName);
            }
            if (!source_identities.emplace(
                                      frontend::code_coverage_source_identity_hex(
                                          source.identity))
                    .second) {
                return reject(Error::DuplicateSourceIdentity);
            }
        }

        const auto declaration_count = ports.size()
            + architecture.signals.size() + architecture.variables.size();
        if (declaration_count > limits.maximum_objects) {
            return reject(Error::ResourceLimit);
        }
        std::vector<PendingObject> pending;
        pending.reserve(declaration_count);
        std::set<std::string> hierarchy_paths;
        std::set<std::pair<std::uint64_t, std::uint64_t>> point_ids;
        std::size_t total_bits = 0U;
        std::size_t visited = 0U;

        const auto append = [&](const std::string_view name,
                                const frontend::Type& type,
                                const frontend::SourceSpan& source_span,
                                const VhdlToggleObjectKind kind) -> Error {
            const auto index = visited++;
            if (!is_vhdl_toggle_type(type)) {
                return Error::None;
            }
            if (name.empty()) {
                result.object_index = index;
                return Error::EmptyObjectName;
            }
            if (invalid_text(name)
                || name.size() > limits.maximum_object_name_bytes) {
                result.object_index = index;
                return name.size() > limits.maximum_object_name_bytes
                    ? Error::ResourceLimit
                    : Error::InvalidObjectName;
            }
            std::string hierarchy_path;
            if (name.size() > limits.maximum_hierarchy_bytes
                || owner.instance.size()
                    > limits.maximum_hierarchy_bytes - name.size()
                || owner.instance.size() + name.size()
                    >= limits.maximum_hierarchy_bytes) {
                result.object_index = index;
                return Error::ResourceLimit;
            }
            hierarchy_path.reserve(owner.instance.size() + name.size() + 1U);
            hierarchy_path.append(owner.instance);
            hierarchy_path.push_back('.');
            hierarchy_path.append(name);
            if (!hierarchy_paths.emplace(hierarchy_path).second) {
                result.object_index = index;
                return Error::DuplicateObjectPath;
            }

            const auto physical_name = frontend::physical_source(source_span);
            const auto source = source_by_name.find(physical_name);
            if (source == source_by_name.end()) {
                result.object_index = index;
                return Error::UnknownObjectSource;
            }
            if (physical_name != owner.source
                && std::ranges::find(owner.source_dependencies, physical_name)
                    == owner.source_dependencies.end()) {
                result.object_index = index;
                return Error::ObjectSourceOwnershipMismatch;
            }
            const frontend::CodeCoverageSourceSpan span {
                static_cast<std::uint64_t>(source_span.begin.offset),
                static_cast<std::uint64_t>(source_span.end.offset),
            };
            const auto source_point
                = frontend::make_code_coverage_point_identity(
                    sources[source->second].identity,
                    frontend::CodeCoverageLanguage::Vhdl,
                    frontend::CodeCoverageConstructKind::ToggleObject, span);
            if (!source_point.ok()) {
                result.object_index = index;
                return Error::InvalidObjectSpan;
            }
            if (source_span.begin.line == 0U
                || source_span.begin.line > limits.maximum_line_number) {
                result.object_index = index;
                return Error::InvalidObjectLine;
            }
            const auto width = type.width();
            if (!width) {
                result.object_index = index;
                return Error::UnspecializedObjectWidth;
            }
            if (*width == 0U
                || *width > limits.maximum_object_width
                || *width > std::numeric_limits<std::size_t>::max()) {
                result.object_index = index;
                return *width > limits.maximum_object_width
                    ? Error::ResourceLimit
                    : Error::InvalidObjectWidth;
            }
            const auto width_value = static_cast<std::size_t>(*width);
            if (width_value > limits.maximum_bits - total_bits) {
                result.object_index = index;
                return Error::ResourceLimit;
            }
            total_bits += width_value;
            const auto point = instance_point_identity(*source_point.identity,
                *instance.identity, kind, hierarchy_path);
            if (!runtime::is_code_coverage_identity_valid(point)
                || !point_ids.emplace(point.high, point.low).second) {
                result.object_index = index;
                return Error::DuplicateObjectIdentity;
            }
            pending.push_back(PendingObject { *source_point.identity, point,
                std::move(hierarchy_path), kind, source->second, span,
                static_cast<std::uint64_t>(source_span.begin.line),
                width_value });
            return Error::None;
        };

        for (const auto& port : ports) {
            if (const auto error = append(port.name, port.type, port.span,
                    VhdlToggleObjectKind::Port);
                error != Error::None) {
                return reject(error, result.object_index);
            }
        }
        for (const auto& signal : architecture.signals) {
            if (const auto error = append(signal.name, signal.type, signal.span,
                    VhdlToggleObjectKind::Signal);
                error != Error::None) {
                return reject(error, result.object_index);
            }
        }
        for (const auto& variable : architecture.variables) {
            if (!variable.vhdl_shared || variable.vhdl_file) {
                ++visited;
                continue;
            }
            if (const auto error = append(variable.name, variable.type,
                    variable.span, VhdlToggleObjectKind::RetainedVariable);
                error != Error::None) {
                return reject(error, result.object_index);
            }
        }

        std::ranges::sort(pending, object_less);
        VhdlToggleInventory inventory;
        inventory.instance_identity = *instance.identity;
        inventory.specialization = owner.specialization;
        inventory.instance = owner.instance;
        inventory.standard = architecture.vhdl_standard;
        inventory.objects.reserve(pending.size());
        inventory.outcomes.reserve(total_bits);
        for (auto& object : pending) {
            const auto first_outcome = inventory.outcomes.size();
            inventory.objects.push_back(VhdlToggleObject {
                object.source_point, object.point, *instance.identity,
                owner.specialization, std::move(object.hierarchy_path),
                object.kind, object.source_index, object.span, object.line,
                object.width, first_outcome });
            for (std::size_t bit = 0U; bit < object.width; ++bit) {
                inventory.outcomes.push_back(
                    runtime::CoverageToggleOutcome { object.point, bit });
            }
        }
        result.inventory = std::move(inventory);
        result.error = Error::None;
        result.object_index = 0U;
        return result;
    } catch (const std::bad_alloc&) {
        return reject(Error::ResourceLimit, result.object_index);
    } catch (const std::length_error&) {
        return reject(Error::ResourceLimit, result.object_index);
    }
}

} // namespace fsim::elaboration
