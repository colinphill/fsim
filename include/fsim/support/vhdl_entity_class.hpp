// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string_view>
#include <utility>

namespace fsim::support {

/// The entity class of a VHDL attribute specification (IEEE 1076-2019 7.2).
enum class VhdlEntityClass : std::uint8_t {
    none,
    entity,
    architecture,
    configuration,
    procedure,
    function,
    package,
    type,
    subtype,
    constant,
    signal,
    variable,
    component,
    label,
    literal,
    units,
    group,
    file,
    property,
    sequence,
    view,
};

inline constexpr VhdlEntityClass kLastVhdlEntityClass = VhdlEntityClass::view;

/// What an attribute specification's entity name list designates (7.2):
/// the listed entity designators, or every (`all`) or every remaining
/// (`others`) named entity of the entity class.
enum class VhdlEntityNameList : std::uint8_t {
    designators,
    all,
    others,
};

/// The entity class a lower-case reserved word names; `none` for any other
/// word.
[[nodiscard]] constexpr VhdlEntityClass vhdl_entity_class(
    const std::string_view word) noexcept
{
    constexpr std::pair<std::string_view, VhdlEntityClass> classes[] {
        { "entity", VhdlEntityClass::entity },
        { "architecture", VhdlEntityClass::architecture },
        { "configuration", VhdlEntityClass::configuration },
        { "procedure", VhdlEntityClass::procedure },
        { "function", VhdlEntityClass::function },
        { "package", VhdlEntityClass::package },
        { "type", VhdlEntityClass::type },
        { "subtype", VhdlEntityClass::subtype },
        { "constant", VhdlEntityClass::constant },
        { "signal", VhdlEntityClass::signal },
        { "variable", VhdlEntityClass::variable },
        { "component", VhdlEntityClass::component },
        { "label", VhdlEntityClass::label },
        { "literal", VhdlEntityClass::literal },
        { "units", VhdlEntityClass::units },
        { "group", VhdlEntityClass::group },
        { "file", VhdlEntityClass::file },
        { "property", VhdlEntityClass::property },
        { "sequence", VhdlEntityClass::sequence },
        { "view", VhdlEntityClass::view },
    };
    for (const auto& [keyword, entity_class] : classes) {
        if (keyword == word) {
            return entity_class;
        }
    }
    return VhdlEntityClass::none;
}

} // namespace fsim::support
