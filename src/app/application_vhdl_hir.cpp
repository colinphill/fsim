// SPDX-License-Identifier: Apache-2.0
#include "application_vhdl_hir_internal.hpp"

#include <functional>

namespace fsim::app::application_detail {
namespace vh = semantic::vhdl;

[[nodiscard]] std::string canonical_vhdl_name(std::string value)
{
    if (!value.empty() && value.front() == '\'') {
        return value;
    }
    std::string result;
    result.reserve(value.size());
    bool in_extended_identifier { };
    for (std::size_t index { }; index < value.size(); ++index) {
        const auto character = value[index];
        if (character == '\\') {
            result.push_back(character);
            if (in_extended_identifier && index + 1U < value.size()
                && value[index + 1U] == '\\') {
                result.push_back(value[++index]);
                continue;
            }
            in_extended_identifier = !in_extended_identifier;
            continue;
        }
        result.push_back(in_extended_identifier
                ? character
                : static_cast<char>(std::tolower(
                      static_cast<unsigned char>(character))));
    }
    return result;
}

std::vector<std::string_view> vhdl_name_parts(const std::string_view value)
{
    std::vector<std::string_view> result;
    std::size_t begin { };
    bool in_extended_identifier { };
    for (std::size_t index { }; index < value.size(); ++index) {
        if (value[index] == '\\') {
            if (in_extended_identifier && index + 1U < value.size()
                && value[index + 1U] == '\\') {
                ++index;
                continue;
            }
            in_extended_identifier = !in_extended_identifier;
            continue;
        }
        if (value[index] != '.' || in_extended_identifier) {
            continue;
        }
        if (index == begin) {
            return { };
        }
        result.push_back(value.substr(begin, index - begin));
        begin = index + 1U;
    }
    if (begin == value.size()) {
        return { };
    }
    result.push_back(value.substr(begin));
    return result;
}

bool same_vhdl_unit_name(
    const vh::Unit& unit, const std::string_view spelling)
{
    const bool extended = spelling.size() >= 2U
        && spelling.front() == '\\' && spelling.back() == '\\';
    if (unit.extended_name != extended) {
        return false;
    }
    if (!extended) {
        return canonical_vhdl_name(unit.name)
            == canonical_vhdl_name(std::string { spelling });
    }
    std::string decoded;
    decoded.reserve(spelling.size() - 2U);
    for (std::size_t index = 1U; index + 1U < spelling.size(); ++index) {
        decoded.push_back(spelling[index]);
        if (spelling[index] == '\\' && index + 2U < spelling.size()
            && spelling[index + 1U] == '\\') {
            ++index;
        }
    }
    return unit.name == decoded;
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
    case frontend::InterfaceObjectClass::Signal:
        return vh::ObjectClass::signal;
    }
    return vh::ObjectClass::constant;
}

semantic::vhdl::Hir build_vhdl_hir(
    const frontend::ParsedDesign& parsed,
    semantic::Model& semantics)
{
    semantic::vhdl::Hir result;
    VhdlHirBuilder builder { semantics, result };
    builder.add_design(parsed);
    complete_vhdl_executable_hir(parsed, semantics, result);
    return result;
}

} // namespace fsim::app::application_detail
