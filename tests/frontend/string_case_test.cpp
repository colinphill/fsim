// SPDX-License-Identifier: Apache-2.0
#include "string_case.hpp"

#include <cassert>
#include <cctype>
#include <string_view>

int main()
{
    using fsim::frontend::detail::ctype_lower_char;
    using fsim::frontend::detail::ctype_lower_copy;
    using fsim::frontend::detail::ctype_upper_equal;

    assert(ctype_lower_char('A') == 'a');
    assert(ctype_lower_char('Z') == 'z');
    assert(ctype_lower_char('_') == '_');
    const auto high_byte = static_cast<char>(0xffU);
    assert(ctype_lower_char(high_byte)
        == static_cast<char>(std::tolower(
            static_cast<unsigned char>(high_byte))));
    assert(ctype_lower_copy("VHDL-2008_SDF") == "vhdl-2008_sdf");
    assert(ctype_lower_copy(std::string_view { }).empty());
    assert(ctype_upper_equal("dElAy", "DELAY"));
    assert(ctype_upper_equal("A_b", "a_B"));
    assert(!ctype_upper_equal("DELAY", "DELAYFILE"));
    assert(!ctype_upper_equal("A", "B"));
}
