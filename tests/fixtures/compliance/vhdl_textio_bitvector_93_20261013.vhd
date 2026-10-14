-- fsim: top=vhdl_textio_bitvector_93_20261013(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 14.3: BIT_VECTOR READ accepts only binary digits.
use std.textio.all;

entity vhdl_textio_bitvector_93_20261013 is
end entity;

architecture sim of vhdl_textio_bitvector_93_20261013 is
begin
  check : process is
    variable l : line;
    variable good : boolean;
    variable bits : bit_vector(1 to 4);
  begin
    write(l, string'("10_01tail"));
    read(l, bits, good);
    if good or l.all /= "_01tail" then
      report "FAIL" severity error;
    else
      report "PASS";
    end if;
    deallocate(l);
    wait;
  end process;
end architecture;
