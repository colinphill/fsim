-- fsim: top=b57_textio_dynamic_field(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
library std;
use std.textio.all;

entity b57_textio_dynamic_field is
end entity;

architecture test of b57_textio_dynamic_field is
begin
    process is
        variable l : line;
        variable width : natural := 0;
    begin
        -- Keep the existing static-width operation as a control.
        write(l, string'("xy"), right, 4);
        assert l.all = "  xy"
            report "static TextIO string field changed" severity error;
        deallocate(l);

        width := 0;
        write(l, string'("xy"), right, width);
        assert l.all = "xy"
            report "dynamic zero-width string field is wrong" severity error;
        deallocate(l);

        width := 4;
        write(l, string'("xy"), right, width);
        assert l.all = "  xy"
            report "dynamic right-justified string field is wrong" severity error;
        deallocate(l);

        write(l, string'("xy"), left, width);
        assert l.all = "xy  "
            report "dynamic left-justified string field is wrong" severity error;
        deallocate(l);

        write(l, bit'('1'), right, width);
        assert l.all = "   1"
            report "dynamic BIT field is wrong" severity error;
        deallocate(l);

        write(l, bit_vector'("101"), right, width);
        assert l.all = " 101"
            report "dynamic BIT_VECTOR field is wrong" severity error;
        deallocate(l);

        write(l, integer'(23), right, width);
        assert l.all = "  23"
            report "dynamic INTEGER field is wrong" severity error;
        deallocate(l);

        width := 6;
        write(l, boolean'(true), left, width);
        assert l.all = "TRUE  "
            report "dynamic BOOLEAN field is wrong" severity error;
        deallocate(l);

        width := 8;
        write(l, time'(1 ns), right, width);
        assert l.all = "    1 ns"
            report "dynamic TIME field is wrong" severity error;
        deallocate(l);

        report "PASS" severity note;
        wait;
    end process;
end architecture;
