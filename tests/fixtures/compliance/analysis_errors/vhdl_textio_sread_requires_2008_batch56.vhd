-- fsim-expect: FSIM-ELAB-VHSTD-001 std=2002
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 16.4: SREAD is a TEXTIO operation added in VHDL-2008.
use std.textio.all;

entity vhdl_textio_sread_requires_2008_batch56 is
end entity;

architecture sim of vhdl_textio_sread_requires_2008_batch56 is
begin
  check : process is
    variable l : line;
    variable value : string(1 to 1);
    variable count : natural;
  begin
    sread(l, value, count);
    string_read(l, value, count);
    wait;
  end process;
end architecture;
