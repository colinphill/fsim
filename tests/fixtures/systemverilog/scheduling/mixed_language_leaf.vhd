-- SPDX-License-Identifier: Apache-2.0
library ieee;
use ieee.std_logic_1164.all;

entity mixed_language_leaf is
  port (
    a : in std_logic;
    y : out std_logic
  );
end entity;

architecture rtl of mixed_language_leaf is
  signal middle : std_logic := '0';
begin
  middle <= a;
  y <= middle;

  process
  begin
    wait until a = '1';
    wait for 0 ns;
    if middle /= '1' or y /= '0' then
      assert false
        report "VHDL first projected cycle did not remain distinct"
        severity failure;
    end if;
    wait for 0 ns;
    if y /= '1' then
      assert false
        report "VHDL second projected cycle did not settle"
        severity failure;
    end if;
    wait;
  end process;
end architecture;
