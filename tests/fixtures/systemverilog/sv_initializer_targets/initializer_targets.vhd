-- SPDX-License-Identifier: Apache-2.0
-- The VHDL half of initializer_targets: each process waits for a transaction
-- on its port for 1 ns. A static variable initializer of the SystemVerilog
-- driver is initialization, not a transaction (IEEE 1800-2023 6.8), so no
-- process wakes before the timeout.
library ieee;
use ieee.std_logic_1164.all;

entity initializer_leaf is
  port (r : in std_logic; p : in std_logic);
end entity;

architecture rtl of initializer_leaf is
begin
  process begin
    wait on r'transaction for 1 ns;
    report "sit vhdl r woke at " & time'image(now) & " r=" & std_logic'image(r);
    wait;
  end process;
  process begin
    wait on p'transaction for 1 ns;
    report "sit vhdl p woke at " & time'image(now) & " p=" & std_logic'image(p);
    wait;
  end process;
end architecture;
