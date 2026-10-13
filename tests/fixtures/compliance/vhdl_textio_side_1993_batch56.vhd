-- fsim: top=vhdl_textio_side_1993_batch56(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 14.3: STD.TEXTIO declares SIDE with RIGHT and LEFT literals.
use std.textio.all;

entity vhdl_textio_side_1993_batch56 is
end entity;

architecture sim of vhdl_textio_side_1993_batch56 is
begin
  check : process is
    constant right_side : side := right;
    constant left_side : side := left;
  begin
    assert right_side = right
      report "TextIO RIGHT did not resolve as a SIDE literal"
      severity failure;
    assert left_side = left
      report "TextIO LEFT did not resolve as a SIDE literal"
      severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
