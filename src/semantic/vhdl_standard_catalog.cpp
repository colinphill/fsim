// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/vhdl_standard_catalog.hpp"

namespace fsim::semantic {
namespace {

constexpr VhdlStandardCatalogEntry catalog[] = {
#include "fsim/semantic/vhdl_standard_catalog.inc"
};

} // namespace

std::span<const VhdlStandardCatalogEntry> vhdl_standard_catalog() noexcept
{
    return catalog;
}

} // namespace fsim::semantic
