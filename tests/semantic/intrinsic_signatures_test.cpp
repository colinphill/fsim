// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/vhdl_intrinsic_signatures.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using fsim::semantic::VhdlIntrinsicSignature;
using fsim::semantic::vhdl_intrinsic_formals;
using fsim::semantic::vhdl_intrinsic_signatures;

std::vector<const VhdlIntrinsicSignature*> find(
    const std::string_view package, const std::string_view name)
{
    std::vector<const VhdlIntrinsicSignature*> result;
    for (const auto& signature : vhdl_intrinsic_signatures()) {
        if (signature.package == package && signature.name == name) {
            result.push_back(&signature);
        }
    }
    return result;
}

bool has_overload(const std::vector<const VhdlIntrinsicSignature*>& found,
    const std::string_view result,
    const std::vector<std::string_view>& formal_types)
{
    return std::ranges::any_of(found, [&](const VhdlIntrinsicSignature* each) {
        const auto formals = vhdl_intrinsic_formals(*each);
        return each->result == result && formals.size() == formal_types.size()
            && std::ranges::equal(formals, formal_types,
                [](const auto& formal, const std::string_view type) {
                    return formal.type == type;
                });
    });
}

} // namespace

int main()
{
    // IEEE 1076-2019 16.7: RESOLVED of a STD_ULOGIC_VECTOR.
    const auto resolved = find("std_logic_1164", "resolved");
    assert(resolved.size() == 1U);
    assert(has_overload(resolved, "std_ulogic", { "std_ulogic_vector" }));

    // 16.8.5: STD_MATCH has four overloads, all returning BOOLEAN.
    const auto match = find("numeric_std", "std_match");
    assert(match.size() == 4U);
    assert(has_overload(match, "boolean", { "std_ulogic", "std_ulogic" }));
    assert(has_overload(match, "boolean",
        { "unresolved_unsigned", "unresolved_unsigned" }));
    assert(has_overload(match, "boolean",
        { "unresolved_signed", "unresolved_signed" }));
    assert(has_overload(match, "boolean",
        { "std_ulogic_vector", "std_ulogic_vector" }));

    // FIND_LEFTMOST / FIND_RIGHTMOST of UNSIGNED and SIGNED.
    for (const auto name : { "find_leftmost", "find_rightmost" }) {
        const auto found = find("numeric_std", name);
        assert(found.size() == 2U);
        assert(has_overload(found, "integer",
            { "unresolved_unsigned", "std_ulogic" }));
        assert(has_overload(found, "integer",
            { "unresolved_signed", "std_ulogic" }));
        assert(found.front()->since == "2008");
    }

    // The mixed operator `unsigned + std_ulogic` and an operator's name.
    const auto plus = find("numeric_std", "+");
    assert(has_overload(plus, "unresolved_unsigned",
        { "unresolved_unsigned", "std_ulogic" }));

    // TO_X01 maps STD_ULOGIC and BIT (and their vectors) to X01 values.
    const auto to_x01 = find("std_logic_1164", "to_x01");
    assert(has_overload(to_x01, "x01", { "std_ulogic" }));
    assert(has_overload(to_x01, "x01", { "bit" }));
    assert(has_overload(to_x01, "std_ulogic_vector", { "std_ulogic_vector" }));

    // Procedures have no result; the defaults are flagged.
    const auto readline = find("textio", "readline");
    assert(readline.size() == 1U && readline.front()->procedure);
    assert(readline.front()->result.empty());
    const auto to_bit = find("std_logic_1164", "to_bit");
    assert(!to_bit.empty());
    assert(std::ranges::any_of(to_bit, [](const VhdlIntrinsicSignature* each) {
        const auto formals = vhdl_intrinsic_formals(*each);
        return formals.size() == 2U && formals.back().has_default;
    }));

    // STD.STANDARD's NOW is an impure function; ENV is a 2008 package.
    const auto now = find("standard", "now");
    assert(now.size() == 1U && now.front()->impure);
    assert(find("env", "stop").front()->since == "2008");

    // The per-overload revision map (scripts/vhdl_intrinsic_revisions.tsv,
    // derived from the 1993, 2008 and 2019 package texts) pins how many
    // overloads each revision first declares.
    struct Count {
        std::string_view package;
        std::string_view since;
        std::size_t overloads;
    };
    const Count expected[] = {
        { "std_logic_1164", "1987", 36 }, { "std_logic_1164", "1993", 2 },
        { "std_logic_1164", "2008", 41 },
        { "numeric_std", "1987", 104 }, { "numeric_std", "1993", 10 },
        { "numeric_std", "2008", 136 },
        { "numeric_bit", "1987", 100 }, { "numeric_bit", "1993", 10 },
        { "numeric_bit", "2008", 126 },
        { "numeric_std_unsigned", "2008", 79 },
        { "numeric_bit_unsigned", "2008", 81 },
        { "math_real", "1987", 31 }, { "math_complex", "1987", 58 },
        { "textio", "1987", 26 }, { "textio", "2008", 9 },
        { "textio", "2019", 1 }, { "env", "2008", 8 }, { "env", "2019", 67 },
        { "standard", "1987", 1 },
    };
    for (const auto& each : expected) {
        const auto count = static_cast<std::size_t>(std::ranges::count_if(
            vhdl_intrinsic_signatures(),
            [&](const VhdlIntrinsicSignature& signature) {
                return signature.package == each.package
                    && signature.since == each.since;
            }));
        if (count != each.overloads) {
            std::cerr << each.package << ' ' << each.since << ": " << count
                      << " overloads, expected " << each.overloads << '\n';
        }
        assert(count == each.overloads);
    }
    // STD_MATCH of UNSIGNED existed in the 1993 packages; of STD_ULOGIC_VECTOR
    // its text declares it too (derived), FIND_LEFTMOST is 2008.
    for (const auto* each : find("numeric_std", "std_match")) {
        assert(each->since == "1987");
    }

    std::cout << "intrinsic signature tests passed\n";
    return 0;
}
