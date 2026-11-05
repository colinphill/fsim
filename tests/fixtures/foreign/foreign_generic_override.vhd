-- SPDX-License-Identifier: Apache-2.0
-- Top-level generic overrides of every scalar class and of BIT and
-- STD_ULOGIC arrays (IEEE 1076-2019 14.2): the values are printed.
package override_types is
  type colour is (red, green, blue);
  type distance is range 0 to 1000000
    units
      mm;
      cm = 10 mm;
      m = 1000 mm;
    end units;
end package;

library ieee;
use ieee.std_logic_1164.all;
use std.textio.all;
use work.override_types.all;

entity top is
  generic (
    R : real := 0.0;
    T : time := 0 ns;
    D : distance := 0 mm;
    E : colour := red;
    C : character := 'a';
    B : bit := '0';
    S : std_ulogic := '0';
    V : std_logic_vector := "0";
    W : bit_vector(3 downto 0) := "0000";
    I : integer := 0);
end entity;

architecture sim of top is
  constant HALF : real := R / 2.0;
begin
  process
    variable l : line;
  begin
    write(l, string'("#c R="));
    write(l, R, right, 0, 3);
    writeline(output, l);
    write(l, string'("#c HALF="));
    write(l, HALF, right, 0, 3);
    writeline(output, l);
    write(l, string'("#c T_ps=") & integer'image(T / 1 ps));
    writeline(output, l);
    write(l, string'("#c D_mm=") & integer'image(D / 1 mm));
    writeline(output, l);
    write(l, string'("#c E=") & colour'image(E));
    writeline(output, l);
    write(l, string'("#c C=") & character'image(C));
    writeline(output, l);
    write(l, string'("#c B=") & bit'image(B));
    writeline(output, l);
    write(l, string'("#c S=") & std_ulogic'image(S));
    writeline(output, l);
    write(l, string'("#c V=") & to_string(V) & " left="
      & integer'image(V'left) & " right=" & integer'image(V'right));
    writeline(output, l);
    write(l, string'("#c W=") & to_string(W));
    writeline(output, l);
    write(l, string'("#c I=") & integer'image(I));
    writeline(output, l);
    wait;
  end process;
end architecture;
