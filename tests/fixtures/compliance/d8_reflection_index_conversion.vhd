-- fsim: top=d8_reflection_index_conversion(test) std=2019
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.3.6, 16.12: INDEX of the compiler-served STD.REFLECTION is
-- an integer type; `index(1)` is a type conversion to it, whose mark has no
-- HIR declaration (a member of a served package).
use std.reflection.all;

entity d8_reflection_index_conversion is
end entity;

architecture test of d8_reflection_index_conversion is
begin
    process
        variable i : index;
    begin
        i := index(-1);
        assert i = -1 report "FAIL: index(-1)" severity failure;
        i := index(3);
        assert i = 3 report "FAIL: index(3)" severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
