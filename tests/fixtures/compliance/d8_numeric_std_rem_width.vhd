-- fsim: top=d8_numeric_std_rem_width(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 16.8.5.3 (numeric_std): "/" of two UNSIGNED or SIGNED
-- values returns the length of its left operand, "rem" and "mod" the length
-- of their right operand, so the remainder of a ten bit value by a four bit
-- one is assigned to a four bit variable without a width mismatch.
library ieee;
use ieee.numeric_std.all;

entity d8_numeric_std_rem_width is
end entity;

architecture test of d8_numeric_std_rem_width is
begin
  process
    variable uns10, ures10 : unsigned(9 downto 0);
    variable uns4, ures4 : unsigned(3 downto 0);
    variable sgn10 : signed(9 downto 0);
    variable sgn4, sres4 : signed(3 downto 0);
  begin
    for i in 1 to 200 loop
      uns10 := to_unsigned(i, 10);
      for j in 1 to 15 loop
        uns4 := to_unsigned(j, 4);
        ures4 := uns10 rem uns4;
        assert to_integer(ures4) = i rem j severity failure;
        ures4 := uns10 mod uns4;
        assert to_integer(ures4) = i mod j severity failure;
        ures4 := uns4 / uns10;
        assert to_integer(ures4) = j / i severity failure;
        ures10 := uns10 rem j;
        assert to_integer(ures10) = i rem j severity failure;
        ures10 := i rem ("000000" & uns4);
        assert to_integer(ures10) = i rem j severity failure;
      end loop;
    end loop;
    sgn10 := to_signed(-77, 10);
    sgn4 := to_signed(5, 4);
    sres4 := sgn10 rem sgn4;
    assert to_integer(sres4) = (-77) rem 5 severity failure;
    sres4 := sgn10 mod sgn4;
    assert to_integer(sres4) = (-77) mod 5 severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
