// SPDX-License-Identifier: Apache-2.0
#include "fsim/support/ctype_whitespace.hpp"

#include <cassert>
#include <string_view>

int main()
{
    constexpr std::string_view padded = "\t \nrecord\v\f\r";
    const auto trimmed = fsim::support::trim_ctype_whitespace(padded);
    assert(trimmed == "record");
    assert(trimmed.data() == padded.data() + 3U);
    assert(fsim::support::trim_ctype_whitespace("record") == "record");
    assert(fsim::support::trim_ctype_whitespace("").empty());
    assert(fsim::support::trim_ctype_whitespace(" \t\r\n").empty());
    assert(fsim::support::trim_ctype_whitespace("a b") == "a b");
}
