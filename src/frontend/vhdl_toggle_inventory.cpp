// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/vhdl_toggle_inventory.hpp"
#include "toggle_inventory_internal.hpp"

#include "fsim/frontend/source.hpp"
#include "fsim/support/identity128.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <new>
#include <ranges>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace fsim::frontend {

using namespace fsim::elaboration;
namespace {

    using Error = VhdlToggleInventoryError;
    using Collector = toggle_inventory_detail::ToggleObjectCollector<Error,
        VhdlToggleObjectKind, VhdlToggleInventoryLimits>;

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
        Collector collector { owner, sources, limits,
            kVhdlToggleInventorySchema, frontend::CodeCoverageLanguage::Vhdl,
            is_vhdl_toggle_type };
        if (const auto error = collector.begin(
                vhdl_unit_identity(architecture), unit_library,
                ports.size() + architecture.signals.size()
                    + architecture.variables.size());
            error != Error::None) {
            return reject(error);
        }
        const auto append = [&](const auto& declaration,
                                const VhdlToggleObjectKind kind) {
            return collector.append(
                declaration.name, declaration.type, declaration.span, kind);
        };
        for (const auto& port : ports) {
            if (const auto error = append(port, VhdlToggleObjectKind::Port);
                error != Error::None) {
                return reject(error, collector.object_index());
            }
        }
        for (const auto& signal : architecture.signals) {
            if (const auto error = append(signal, VhdlToggleObjectKind::Signal);
                error != Error::None) {
                return reject(error, collector.object_index());
            }
        }
        for (const auto& variable : architecture.variables) {
            if (!variable.vhdl_shared || variable.vhdl_file) {
                collector.skip();
                continue;
            }
            if (const auto error = append(
                    variable, VhdlToggleObjectKind::RetainedVariable);
                error != Error::None) {
                return reject(error, collector.object_index());
            }
        }

        VhdlToggleInventory inventory;
        inventory.instance_identity = collector.instance();
        inventory.specialization = owner.specialization;
        inventory.instance = owner.instance;
        inventory.standard = architecture.vhdl_standard;
        auto& pending = collector.sorted_objects();
        inventory.objects.reserve(pending.size());
        inventory.outcomes.reserve(collector.total_bits());
        for (auto& object : pending) {
            const auto first_outcome = inventory.outcomes.size();
            inventory.objects.push_back(VhdlToggleObject {
                object.source_point, object.point, collector.instance(),
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
        return reject(Error::ResourceLimit);
    } catch (const std::length_error&) {
        return reject(Error::ResourceLimit);
    }
}

} // namespace fsim::frontend
