-- SPDX-License-Identifier: Apache-2.0
-- A tri-state driver, the VHDL source of net_sources.sv.
library ieee;
use ieee.std_logic_1164.all;

entity vdrv is
  port (a : in std_logic; en : in std_logic; z : out std_logic);
end entity;

architecture rtl of vdrv is
begin
  z <= a when en = '1' else 'Z';
end architecture;
