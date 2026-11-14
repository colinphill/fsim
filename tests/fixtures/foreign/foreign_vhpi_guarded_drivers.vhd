-- SPDX-License-Identifier: Apache-2.0
-- Drivers of the unlabeled concurrent statements of a guarded block (IEEE
-- 1076-2019 11.2, 11.6, 14.7.2): each statement's equivalent process holds
-- its own drivers, which report their transactions and value changes
-- (21.3.2.2, 21.3.2.5). The block's implicit GUARD signal is no statement
-- of the block, so it does not shift the statements that follow it. A
-- guarded signal's driver is disconnected (a null transaction) while GUARD
-- is FALSE; an ordinary signal's guarded driver keeps its value then.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture a of top is
  signal en : bit := '0';
  signal r : std_logic_vector(1 downto 0) := "00";
  signal v : std_logic_vector(1 downto 0) bus := "ZZ";
  signal t : bit := '0';
  signal i : integer := 0;
begin
  b : block (en = '1') is
  begin
    v <= guarded "10";
    t <= guarded '1';
    r <= "11" when en = '1' else "01";
    i <= guarded 7;
  end block;
  pe : process is
  begin
    wait for 1 ns;
    en <= '1';
    wait for 1 ns;
    en <= '0';
    wait;
  end process;
end architecture;
