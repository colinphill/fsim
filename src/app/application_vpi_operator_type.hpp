// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "sv_vpi_user.h"

#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>

namespace fsim::app::application_detail {

// The operator of a unary, binary or update expression (IEEE 1800-2023
// 37.78 vpiOpType).
[[nodiscard]] inline std::optional<PLI_INT32> operator_type(
    const std::string_view text, const std::size_t operands)
{
    if (operands == 1U) {
        static constexpr std::pair<std::string_view, PLI_INT32> unary[] = {
            { "-", vpiMinusOp }, { "+", vpiPlusOp }, { "!", vpiNotOp },
            { "~", vpiBitNegOp }, { "&", vpiUnaryAndOp },
            { "~&", vpiUnaryNandOp }, { "|", vpiUnaryOrOp },
            { "~|", vpiUnaryNorOp }, { "^", vpiUnaryXorOp },
            { "~^", vpiUnaryXNorOp }, { "^~", vpiUnaryXNorOp },
        };
        for (const auto& [spelling, type] : unary) {
            if (text == spelling) {
                return type;
            }
        }
        return std::nullopt;
    }
    if (operands == 3U && text == "?:") {
        return vpiConditionOp;
    }
    static constexpr std::pair<std::string_view, PLI_INT32> binary[] = {
        { "+", vpiAddOp }, { "-", vpiSubOp }, { "*", vpiMultOp },
        { "/", vpiDivOp }, { "%", vpiModOp }, { "**", vpiPowerOp },
        { "<<", vpiLShiftOp }, { ">>", vpiRShiftOp },
        { "<<<", vpiArithLShiftOp }, { ">>>", vpiArithRShiftOp },
        { "==", vpiEqOp }, { "!=", vpiNeqOp }, { "===", vpiCaseEqOp },
        { "!==", vpiCaseNeqOp }, { "==?", vpiWildEqOp },
        { "!=?", vpiWildNeqOp }, { ">", vpiGtOp }, { ">=", vpiGeOp },
        { "<", vpiLtOp }, { "<=", vpiLeOp }, { "&&", vpiLogAndOp },
        { "||", vpiLogOrOp }, { "&", vpiBitAndOp }, { "|", vpiBitOrOp },
        { "^", vpiBitXorOp }, { "~^", vpiBitXNorOp }, { "^~", vpiBitXNorOp },
        { "->", vpiImplyOp }, { "inside", vpiInsideOp },
    };
    for (const auto& [spelling, type] : binary) {
        if (text == spelling) {
            return type;
        }
    }
    return std::nullopt;
}

} // namespace fsim::app::application_detail
