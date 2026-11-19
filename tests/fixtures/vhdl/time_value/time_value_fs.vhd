-- SPDX-License-Identifier: Apache-2.0
-- TIME'VALUE at resolution 1 fs (IEEE 1076-2019 16.2.2, 5.2.4.2). A separate
-- file: its fs literals cannot be represented at the 1 ps resolution of
-- time_value.vhd.

-- Resolution 1 fs: every unit is exact.
entity time_value_fs is
end entity;

architecture test of time_value_fs is
begin
  process
    variable t : time;
  begin
    t := time'value("1 fs");
    report "b1 " & integer'image(t / 1 fs);
    t := time'value("1500 fs");
    report "b2 " & integer'image(t / 1 fs);
    t := time'value(" 10 ns ");
    report "b3 " & integer'image(t / 1 fs);
    wait;
  end process;
end architecture;
