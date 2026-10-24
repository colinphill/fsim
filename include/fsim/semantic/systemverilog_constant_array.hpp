// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fsim::semantic {

class SpecializedHirUnit;

/// The shared SystemVerilog unpacked array constant evaluator, which
/// elaboration provides: the integral value of each element from the left
/// bound, or nullopt when the expression is not a constant array.
using SystemVerilogConstantArray
    = std::optional<std::vector<std::int64_t>> (*)(
        const SpecializedHirUnit& unit, ExpressionId expression);

/// Register the shared array evaluator, which parameter specialization uses
/// for an unpacked array actual that is not a pattern or a name, such as a
/// constant function call (IEEE 1800-2017 13.4.3, 23.10).
void set_systemverilog_constant_array(
    SystemVerilogConstantArray evaluator) noexcept;

/// The shared element evaluator, which elaboration provides: the canonical
/// constant identity of one element of an unpacked array actual in the
/// formal's element type, for an actual whose elements are not all 64-bit
/// integral values (real, string, wide or structure elements, IEEE
/// 1800-2017 6.20.2), or nullopt when the element is not constant.
using SystemVerilogConstantElement = std::optional<std::string> (*)(
    const SpecializedHirUnit& unit, ExpressionId expression,
    const sv::TypeReference& element_type);

/// Register the shared element evaluator.
void set_systemverilog_constant_element(
    SystemVerilogConstantElement evaluator) noexcept;

} // namespace fsim::semantic
