-- SPDX-License-Identifier: Apache-2.0
--
-- It is an error if a source of a resolved signal is a composite where some
-- subelements are determined by the null transaction and some are not
-- (IEEE 1076-2019 14.7.3.2). gp is a bus of pairs resolved as a whole: p1's
-- whole null at 1 ns is fine (its driver is no source, the sum is p2's
-- (10, 20)), as is its value at 2 ns; its null for gp(0) alone at 3 ns
-- leaves gp(1) driven: the run stops with FSIM-RUN-VHDLNULL-001 (checked by
-- run_inline_ops.cmake's FSIM_EXPECT_ERROR), and the report after it never
-- runs.
package partial_null_source_pkg is
  type int2 is array (0 to 1) of integer;
  type int2_vec is array (natural range <>) of int2;
  function pair_sum (v : int2_vec) return int2;
  subtype rpair is pair_sum int2;
end package;

package body partial_null_source_pkg is
  function pair_sum (v : int2_vec) return int2 is
    variable r : int2 := (0, 0);
  begin
    for i in v'range loop
      r(0) := r(0) + v(i)(0);
      r(1) := r(1) + v(i)(1);
    end loop;
    return r;
  end function;
end package body;

use work.partial_null_source_pkg.all;

entity top is
end entity;

architecture a of top is
  signal gp : rpair bus := (1, 2);
begin
  p1 : process
  begin
    gp <= (3, 4);
    wait for 1 ns;
    gp <= null;
    wait for 1 ns;
    gp <= (5, 6);
    wait for 1 ns;
    gp(0) <= null;
    wait for 1 ns;
    report "wide not reached;";
    wait;
  end process;

  p2 : process
  begin
    gp <= (10, 20);
    wait;
  end process;

  mon : process (gp)
  begin
    report "wide gp=" & integer'image(gp(0)) & "," & integer'image(gp(1))
      & " at " & integer'image(now / 1 ns) & ";";
  end process;
end architecture;
