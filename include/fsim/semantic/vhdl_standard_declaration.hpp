// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>

#include "fsim/project/project.hpp"
#include "fsim/semantic/vhdl_hir.hpp"
#include "fsim/support/vhdl_standard_declaration.hpp"

namespace fsim::semantic {

/// The edition year of a standard, as support::available takes it.
[[nodiscard]] constexpr std::uint16_t standard_year(
    const project::VhdlStandard standard)
{
    switch (standard) {
    case project::VhdlStandard::vhdl_1987:
        return 1987U;
    case project::VhdlStandard::vhdl_1993:
        return 1993U;
    case project::VhdlStandard::vhdl_2000:
        return 2000U;
    case project::VhdlStandard::vhdl_2002:
        return 2002U;
    case project::VhdlStandard::vhdl_2008:
        return 2008U;
    case project::VhdlStandard::vhdl_2019:
        return 2019U;
    }
    throw std::invalid_argument("unknown VHDL standard");
}

/// Whether the STANDARD of `standard` has the declaration.
[[nodiscard]] constexpr bool available(
    const support::StandardDeclaration declaration,
    const project::VhdlStandard standard)
{
    return support::available(declaration, standard_year(standard));
}

/// The STD.STANDARD type declaration that a provided-type identity stands
/// for (IEEE 1076-2019 16.3), or nullopt for the identities of other packages
/// (STD_LOGIC_1164, NUMERIC_STD, TEXTIO, ...) and for none.
[[nodiscard]] std::optional<support::StandardDeclaration>
standard_declaration_of(vhdl::ProvidedType type) noexcept;

/// The inverse: the provided-type identity of a STANDARD type declaration, or
/// nullopt where the declaration is not a type the compiler provides
/// natively (an operation, an attribute, a range record, a type without an
/// identity).
[[nodiscard]] std::optional<vhdl::ProvidedType> provided_type_of(
    support::StandardDeclaration declaration) noexcept;

} // namespace fsim::semantic
