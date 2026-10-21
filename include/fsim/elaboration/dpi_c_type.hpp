// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace fsim::elaboration {

// The C side of a DPI-C formal or result type spelled with SystemVerilog
// built-in type keywords and decimal packed ranges.
struct DpiCType {
    std::size_t width { };
    frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
    bool signed_value { };
    bool string { };
    // The C type of the value (IEEE 1800-2023 Annex H.7.4): c s i l
    // for byte, shortint, int and longint (upper case when unsigned),
    // b svBit, g svLogic, d double, f float, p chandle, z string, B an
    // svBitVecVal vector, V an svLogicVecVal vector.
    char code { };
    frontend::SystemVerilogScalarKind scalar {
        frontend::SystemVerilogScalarKind::None
    };
};

[[nodiscard]] std::optional<DpiCType> dpi_c_type(
    std::span<const semantic::sv::SourceToken> tokens);

// A C header declaring every validated DPI-C import and export with its
// Annex H C prototype, like other simulators' DPI header generation. A
// declaration whose types this mapping cannot spell is listed in a comment.
[[nodiscard]] std::string systemverilog_dpi_header(
    const semantic::sv::Hir& hir);

}  // namespace fsim::elaboration
