-- SPDX-License-Identifier: Apache-2.0
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture a of top is
  signal v : std_logic := '0';
begin
  v <= '1' after 5 ns;
end architecture;
