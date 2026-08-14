-- SPDX-License-Identifier: Apache-2.0
library ieee;
use ieee.std_logic_1164.all;

entity vital_cell is
  port (a : in std_logic; z : out std_logic);
end entity;

architecture timing of vital_cell is
begin
  z <= a after 1 ns;
end architecture;
