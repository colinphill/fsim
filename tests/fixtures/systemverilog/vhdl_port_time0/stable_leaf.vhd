-- SPDX-License-Identifier: Apache-2.0
-- S'STABLE and S'QUIET in a sensitivity list wake on the event and again on
-- the return to TRUE (IEEE 1076-2019 16.2.4). The order of the time-0
-- SystemVerilog writes and the VHDL initialization is unspecified, so the
-- watchers report only after time 0 and `sample` reads the port between
-- time steps.
library ieee;
use ieee.std_logic_1164.all;
entity stable_leaf is
  port (p : in std_logic);
end entity;
architecture rtl of stable_leaf is
begin
  watch_quiet : process (p'quiet) begin
    if now > 0 ns then
      report "quiet=" & boolean'image(p'quiet) & " p=" & std_logic'image(p) & " at " & time'image(now);
    end if;
  end process;
  watch_stable : process (p'stable) begin
    if now > 0 ns then
      report "stable=" & boolean'image(p'stable) & " p=" & std_logic'image(p) & " at " & time'image(now);
    end if;
  end process;
  sample : process begin
    wait for 500 ps;
    report "vhdl sample p=" & std_logic'image(p) & " quiet=" & boolean'image(p'quiet)
      & " stable=" & boolean'image(p'stable);
    wait;
  end process;
end architecture;
