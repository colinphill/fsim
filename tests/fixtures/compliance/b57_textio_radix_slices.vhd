-- fsim: top=b57_textio_radix_slices(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
use std.textio.all;

entity b57_textio_radix_slices is
end entity;

architecture test of b57_textio_radix_slices is
begin
    process is
        variable ascending_value  : bit_vector(1 to 8) := "10100101";
        variable descending_value : bit_vector(7 downto 0) := "10100101";
        variable line_value : line;
        variable good       : boolean;
    begin
        line_value := new string'("F");
        hread(line_value, ascending_value(1 to 4));
        assert ascending_value = "11110101";
        assert line_value.all = "";
        deallocate(line_value);

        line_value := new string'("C");
        hread(line_value, descending_value(7 downto 4), good);
        assert good;
        assert descending_value = "11000101";
        assert line_value.all = "";
        deallocate(line_value);

        line_value := new string'("4");
        oread(line_value, ascending_value(1 to 3), good);
        assert good;
        assert ascending_value = "10010101";
        assert line_value.all = "";
        deallocate(line_value);

        line_value := new string'("3");
        oread(line_value, descending_value(7 downto 5));
        assert descending_value = "01100101";
        assert line_value.all = "";
        deallocate(line_value);

        line_value := new string'("G");
        hread(line_value, ascending_value(1 to 4), good);
        assert not good;
        assert ascending_value = "10010101";
        assert line_value.all = "G";
        deallocate(line_value);

        line_value := new string'("8");
        oread(line_value, descending_value(7 downto 5), good);
        assert not good;
        assert descending_value = "01100101";
        assert line_value.all = "8";
        deallocate(line_value);

        report "PASS";
        wait;
    end process;
end architecture;
