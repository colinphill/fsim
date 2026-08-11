// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

    [[nodiscard]] int matching_class(const runtime::Logic9 value)
    {
        if (value == runtime::Logic9::dont_care) {
            return -1;
        }
        if (value == runtime::Logic9::zero || value == runtime::Logic9::l) {
            return 0;
        }
        if (value == runtime::Logic9::one || value == runtime::Logic9::h) {
            return 1;
        }
        return 2;
    }

    [[nodiscard]] bool patterns_overlap(
        const PackedLogic4& lhs,
        const PackedLogic4& rhs)
    {
        for (std::size_t bit = 0; bit < lhs.width(); ++bit) {
            const auto left = matching_class(lhs.get_logic9(bit));
            const auto right = matching_class(rhs.get_logic9(bit));
            if (left != -1 && right != -1
                && (left == 2 || right == 2 || left != right)) {
                return false;
            }
        }
        return true;
    }

} // namespace

bool Lowerer::validate_vhdl_matching_case(
    const Statement& statement,
    const frontend::Type* selector_type,
    const std::size_t selector_width,
    const frontend::ValueDomain selector_domain)
{
    if (language_ != frontend::Language::Vhdl2008) {
        report(
            "FSIM-ELAB-VHDLMATCH-001",
            "matching case and selected assignments require VHDL-2008",
            statement.span);
        return false;
    }
    const bool scalar_selector_is_discrete_nonmatching = statement.condition.kind == ExpressionKind::IntegerLiteral
        || statement.condition.kind == ExpressionKind::BooleanLiteral
        || (selector_type != nullptr
            && selector_type->domain != frontend::ValueDomain::Bit2
            && selector_type->domain != frontend::ValueDomain::Logic9);
    if (selector_width == 0
        || scalar_selector_is_discrete_nonmatching
        || (selector_domain != frontend::ValueDomain::Bit2
            && selector_domain != frontend::ValueDomain::Logic9)) {
        report(
            "FSIM-ELAB-VHDLMATCH-001",
            "a matching case selector must be bit, std_ulogic, or a bounded "
            "one-dimensional array of those element types",
            statement.condition.span);
        return false;
    }

    std::vector<PackedLogic4> prior_patterns;
    bool valid = true;
    for (const auto& alternative : statement.case_alternatives) {
        if (alternative.is_default) {
            continue;
        }
        for (const auto& choice : alternative.choices) {
            const auto literal = literal_value(
                choice, selector_width, frontend::Language::Vhdl2008);
            if (!literal || literal->value.width() != selector_width) {
                report(
                    "FSIM-ELAB-VHDLMATCH-002",
                    "bounded matching choices must be locally static bit or "
                    "std_ulogic literals with the selector width",
                    choice.span);
                valid = false;
                continue;
            }
            const auto pattern = literal->value.promoted_to_logic9();
            if (std::ranges::any_of(
                    prior_patterns,
                    [&](const PackedLogic4& prior) {
                        return patterns_overlap(prior, pattern);
                    })) {
                report(
                    "FSIM-ELAB-VHDLMATCH-003",
                    "matching case choices overlap after '-' wildcard and L/H "
                    "normalization",
                    choice.span);
                valid = false;
            }
            prior_patterns.push_back(pattern);
        }
    }
    return valid;
}

} // namespace fsim::elaboration
