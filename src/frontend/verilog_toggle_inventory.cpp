// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/verilog_toggle_inventory.hpp"
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

    using Error = VerilogToggleInventoryError;
    using Collector = toggle_inventory_detail::ToggleObjectCollector<Error,
        VerilogToggleObjectKind, VerilogToggleInventoryLimits>;

    std::optional<frontend::CodeCoverageLanguage> coverage_language(
        const frontend::Language language) noexcept
    {
        switch (language) {
        case frontend::Language::Verilog2005:
            return frontend::CodeCoverageLanguage::Verilog;
        case frontend::Language::SystemVerilog2017:
            return frontend::CodeCoverageLanguage::SystemVerilog;
        case frontend::Language::Vhdl2008:
            return std::nullopt;
        }
        return std::nullopt;
    }

    bool valid_unit_kind(const frontend::UnitKind kind) noexcept
    {
        switch (kind) {
        case frontend::UnitKind::VerilogModule:
        case frontend::UnitKind::SystemVerilogInterface:
        case frontend::UnitKind::SystemVerilogProgram:
            return true;
        case frontend::UnitKind::VhdlEntity:
        case frontend::UnitKind::VhdlArchitecture:
        case frontend::UnitKind::VhdlConfiguration:
        case frontend::UnitKind::VhdlPackage:
        case frontend::UnitKind::VhdlContext:
        case frontend::UnitKind::SystemVerilogPackage:
        case frontend::UnitKind::VhdlPslVerificationUnit:
        case frontend::UnitKind::SystemVerilogConfiguration:
        case frontend::UnitKind::SystemVerilogBind:
            return false;
        }
        return false;
    }

    std::string verilog_unit_identity(const frontend::DesignUnit& unit)
    {
        const auto library = unit.library.empty()
            ? std::string_view { "work" }
            : std::string_view { unit.library };
        if (unit.kind == frontend::UnitKind::SystemVerilogInterface) {
            return "sv:" + std::string { library } + ".interface(" + unit.name
                + ")";
        }
        if (unit.kind == frontend::UnitKind::SystemVerilogProgram) {
            return "sv:" + std::string { library } + ".program(" + unit.name
                + ")";
        }
        return "sv:" + std::string { library } + "." + unit.name;
    }

    bool is_verilog_net_type(const frontend::Type& type) noexcept
    {
        const auto name = type.systemverilog_net_type.empty()
            ? std::string_view { type.spelling }
            : std::string_view { type.systemverilog_net_type };
        return name == "wire" || name == "tri" || name == "tri0"
            || name == "tri1" || name == "wand" || name == "triand"
            || name == "wor" || name == "trior" || name == "trireg"
            || name == "uwire" || name == "supply0" || name == "supply1"
            || !type.systemverilog_net_type.empty();
    }

    bool is_verilog_variable_type(const frontend::Type& type) noexcept
    {
        const auto name = std::string_view { type.spelling };
        return name == "reg" || name == "logic" || name == "bit"
            || name == "byte" || name == "shortint" || name == "int"
            || name == "longint" || name == "integer" || name == "time";
    }

} // namespace

bool is_verilog_toggle_type(const frontend::Type& type) noexcept
{
    using Scalar = frontend::SystemVerilogScalarKind;
    if (type.systemverilog_container
        || !type.systemverilog_class_declaration.empty()
        || type.systemverilog_virtual_interface
        || !type.systemverilog_interface_type.empty()
        || type.vhdl_access || type.vhdl_file || type.vhdl_protected) {
        return false;
    }
    switch (type.systemverilog_scalar) {
    case Scalar::ShortReal:
    case Scalar::Real:
    case Scalar::Realtime:
    case Scalar::Chandle:
        return false;
    case Scalar::None:
    case Scalar::Time:
        break;
    }
    switch (type.domain) {
    case frontend::ValueDomain::Bit2:
    case frontend::ValueDomain::Logic4:
    case frontend::ValueDomain::Integer:
        return true;
    case frontend::ValueDomain::Logic9:
    case frontend::ValueDomain::Boolean:
    case frontend::ValueDomain::String:
    case frontend::ValueDomain::Unknown:
        return false;
    }
    return false;
}

VerilogToggleInventoryResult make_verilog_toggle_inventory(
    const frontend::DesignUnit& unit,
    const CoverageInventoryOwner& owner,
    const std::span<const VerilogCoverageSource> sources,
    const VerilogToggleInventoryLimits limits) noexcept
{
    VerilogToggleInventoryResult result;
    const auto reject = [&](const Error error,
                            const std::size_t object_index = 0U) {
        result.inventory.reset();
        result.error = error;
        result.object_index = object_index;
        return result;
    };

    try {
        const auto language = coverage_language(unit.language);
        if (!language || owner.language != unit.language) {
            return reject(Error::InvalidLanguage);
        }
        if (!valid_unit_kind(unit.kind)) {
            return reject(Error::InvalidUnitKind);
        }
        const auto unit_library = unit.library.empty()
            ? std::string_view { "work" }
            : std::string_view { unit.library };
        Collector collector { owner, sources, limits,
            kVerilogToggleInventorySchema, *language, is_verilog_toggle_type };
        if (const auto error = collector.begin(verilog_unit_identity(unit),
                unit_library,
                unit.ports.size() + unit.signals.size()
                    + unit.variables.size());
            error != Error::None) {
            return reject(error);
        }
        const auto append = [&](const auto& declaration,
                                const VerilogToggleObjectKind kind) {
            return collector.append(
                declaration.name, declaration.type, declaration.span, kind);
        };
        for (const auto& port : unit.ports) {
            if (const auto error = append(port, VerilogToggleObjectKind::Port);
                error != Error::None) {
                return reject(error, collector.object_index());
            }
        }
        for (const auto& signal : unit.signals) {
            const auto kind = is_verilog_net_type(signal.type)
                ? VerilogToggleObjectKind::Net
                : is_verilog_variable_type(signal.type)
                ? VerilogToggleObjectKind::RetainedVariable
                : VerilogToggleObjectKind::Signal;
            if (const auto error = append(signal, kind);
                error != Error::None) {
                return reject(error, collector.object_index());
            }
        }
        for (const auto& variable : unit.variables) {
            if (const auto error = append(
                    variable, VerilogToggleObjectKind::RetainedVariable);
                error != Error::None) {
                return reject(error, collector.object_index());
            }
        }

        VerilogToggleInventory inventory;
        inventory.instance_identity = collector.instance();
        inventory.specialization = owner.specialization;
        inventory.instance = owner.instance;
        auto& pending = collector.sorted_objects();
        inventory.objects.reserve(pending.size());
        inventory.outcomes.reserve(collector.total_bits());
        for (auto& object : pending) {
            const auto first_outcome = inventory.outcomes.size();
            inventory.objects.push_back(VerilogToggleObject {
                object.source_point, object.point, collector.instance(),
                owner.specialization, std::move(object.hierarchy_path),
                object.kind, *language, object.source_index, object.span,
                object.line, object.width, first_outcome });
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
