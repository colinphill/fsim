// SPDX-License-Identifier: Apache-2.0
#include "vhdl_parser_internal.hpp"

namespace fsim::frontend {

std::optional<std::int64_t>
simple_vhdl_integer_constant(const Expression& expression)
{
    if (expression.kind == ExpressionKind::IntegerLiteral) {
        return decimal_i64(expression.text);
    }
    if (expression.kind == ExpressionKind::Unary
        && expression.operands.size() == 1
        && (expression.text == "+" || expression.text == "-")) {
        if (expression.text == "-"
            && expression.operands.front().kind
                == ExpressionKind::IntegerLiteral) {
            const auto magnitude
                = decimal_u64(expression.operands.front().text);
            if (magnitude && *magnitude == (std::uint64_t { 1 } << 63U)) {
                return std::numeric_limits<std::int64_t>::min();
            }
        }
        const auto magnitude
            = simple_vhdl_integer_constant(expression.operands.front());
        if (!magnitude) {
            return std::nullopt;
        }
        if (expression.text == "+") {
            return magnitude;
        }
        if (*magnitude == std::numeric_limits<std::int64_t>::min()) {
            return std::nullopt;
        }
        return -*magnitude;
    }
    return std::nullopt;
}

} // namespace fsim::frontend
