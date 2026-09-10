// SPDX-License-Identifier: Apache-2.0
#include "application_vhdl_hir_internal.hpp"

#include <functional>

namespace fsim::app::application_detail {
namespace vh = semantic::vhdl;

[[nodiscard]] std::string canonical_vhdl_name(std::string value)
{
    if (!value.empty() && value.front() != '\\' && value.front() != '\'') {
        std::transform(value.begin(), value.end(), value.begin(), [](const char c) {
            return static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
        });
    }
    return value;
}

[[nodiscard]] vh::UnitKind vhdl_unit_kind(
    const frontend::UnitKind kind) noexcept
{
    switch (kind) {
    case frontend::UnitKind::VhdlEntity:
        return vh::UnitKind::entity;
    case frontend::UnitKind::VhdlArchitecture:
        return vh::UnitKind::architecture;
    case frontend::UnitKind::VhdlConfiguration:
        return vh::UnitKind::configuration;
    case frontend::UnitKind::VhdlPackage:
        return vh::UnitKind::package;
    case frontend::UnitKind::VhdlContext:
        return vh::UnitKind::context;
    case frontend::UnitKind::VhdlPslVerificationUnit:
        return vh::UnitKind::psl_verification_unit;
    default:
        return vh::UnitKind::entity;
    }
}

[[nodiscard]] vh::Direction vhdl_direction(
    const frontend::PortDirection direction) noexcept
{
    switch (direction) {
    case frontend::PortDirection::Input:
        return vh::Direction::input;
    case frontend::PortDirection::Output:
        return vh::Direction::output;
    case frontend::PortDirection::Inout:
        return vh::Direction::inout;
    case frontend::PortDirection::Buffer:
        return vh::Direction::buffer;
    default:
        return vh::Direction::unknown;
    }
}

[[nodiscard]] vh::ObjectClass vhdl_object_class(
    const frontend::InterfaceObjectClass object_class) noexcept
{
    switch (object_class) {
    case frontend::InterfaceObjectClass::Constant:
        return vh::ObjectClass::constant;
    case frontend::InterfaceObjectClass::Variable:
        return vh::ObjectClass::variable;
    case frontend::InterfaceObjectClass::File:
        return vh::ObjectClass::file;
    }
    return vh::ObjectClass::constant;
}

semantic::vhdl::Hir build_vhdl_hir(
    const frontend::ParsedDesign& parsed,
    semantic::Model& semantics)
{
    semantic::vhdl::Hir result;
    if (!parsed.vhdl_profile_compatible) {
        return result;
    }
    VhdlHirBuilder builder { semantics, result };
    builder.add_design(parsed);
    complete_vhdl_executable_hir(parsed, semantics, result);
    return result;
}

} // namespace fsim::app::application_detail
