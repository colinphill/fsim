-- SPDX-License-Identifier: Apache-2.0
--
-- Guarded signals of resolved subtypes the design declares, a composite
-- (an array resolved as a whole) and a nine-value scalar (IEEE 1076-2019
-- 4.6, 6.4.2.3). A driver is no source while its value is determined by the
-- null transaction (10.5.2.2, 14.7.3.2): all of a composite's elements null,
-- here through two element assignments in one cycle, or the whole signal;
-- with none connected a bus takes the function's result for a null array.
--   gp (bus of pairs, the pairwise sum; p1 and p2): 0 fs: the drivers'
--     initial values (1, 2) + (1, 2) = (2, 4). Then p1's (3, 4) and p2's
--     (10, 20): (13, 24). 1 ns: p1's gp(0) and gp(1) become null in the
--     same cycle, the whole driver is: (10, 20). 2 ns: p1's (5, 6): (15, 26).
--     3 ns: p1's whole null: (10, 20). 4 ns: p2's null, no driver: the sum
--     of a null array, (0, 0).
--   gl (bus of a wired-and over STD_ULOGIC, 'Z' for no source; q1 and q2,
--     half a nanosecond after gp's processes, so that the two signals do
--     not change in the same cycle): 0 fs: 'U' and 'U' give 'U'; at 0.5 ns
--     q1's '1' and q2's '1': '1'. 1.5 ns: q1's '0': '0'. 2.5 ns: q1's
--     null: only q2's '1': '1' (not '0', the value q1 last drove). 3.5 ns:
--     q2's null: no driver, 'Z'. 4.5 ns: q1's '0': '0'.

library ieee;
use ieee.std_logic_1164.all;

package guarded_user_composite_pkg is
  type int2 is array (0 to 1) of integer;
  type int2_vec is array (natural range <>) of int2;
  function pair_sum (v : int2_vec) return int2;
  subtype rpair is pair_sum int2;
  function wired_and (v : std_ulogic_vector) return std_ulogic;
  subtype rlogic is wired_and std_ulogic;
end package;

package body guarded_user_composite_pkg is
  function pair_sum (v : int2_vec) return int2 is
    variable r : int2 := (0, 0);
  begin
    for i in v'range loop
      r(0) := r(0) + v(i)(0);
      r(1) := r(1) + v(i)(1);
    end loop;
    return r;
  end function;

  function wired_and (v : std_ulogic_vector) return std_ulogic is
    variable r : std_ulogic := '1';
  begin
    if v'length = 0 then
      return 'Z';
    end if;
    for i in v'range loop
      r := r and v(i);
    end loop;
    return r;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.guarded_user_composite_pkg.all;

entity top is
end entity;

architecture a of top is
  signal gp : rpair bus := (1, 2);
  signal gl : rlogic bus := 'U';
begin
  p1 : process
  begin
    gp <= (3, 4);
    wait for 1 ns;
    gp(0) <= null;
    gp(1) <= null;
    wait for 1 ns;
    gp <= (5, 6);
    wait for 1 ns;
    gp <= null;
    wait;
  end process;

  p2 : process
  begin
    gp <= (10, 20);
    wait for 4 ns;
    gp <= null;
    wait;
  end process;

  q1 : process
  begin
    wait for 500 ps;
    gl <= '1';
    wait for 1 ns;
    gl <= '0';
    wait for 1 ns;
    gl <= null;
    wait for 2 ns;
    gl <= '0';
    wait;
  end process;

  q2 : process
  begin
    wait for 500 ps;
    gl <= '1';
    wait for 3 ns;
    gl <= null;
    wait;
  end process;

  mon : process (gp, gl)
  begin
    report "wide gp=" & integer'image(gp(0)) & "," & integer'image(gp(1))
      & " gl=" & std_ulogic'image(gl) & " at "
      & integer'image(now / 1 ps) & ";";
  end process;
end architecture;
