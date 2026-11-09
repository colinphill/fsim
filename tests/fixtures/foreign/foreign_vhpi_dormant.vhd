-- SPDX-License-Identifier: Apache-2.0
-- A foreign architecture's drivers come from vhpi_create (IEEE 1076-2019
-- 20.4.2, 23.6). Each out port of u1 is a source of the signal it is
-- associated with whether or not it has a driver (6.4.2.3); a port with no
-- source drives its default (14.7.3.2 e)). At the start of simulation,
-- before the first cycle (14.7.5.3), y resolves top's initial 'U' with the
-- port's 'U'; q keeps its default; w, resolved by a user function that sums
-- its sources, is top's initial 10 plus the port's default 10. A created
-- driver holds the signal's default until its first transaction (14.7.2) and
-- becomes the port's source (IEEE 1164 resolution table for y; the user
-- function receives its value for w).
library ieee;
use ieee.std_logic_1164.all;

package summing is
  type int_array is array (natural range <>) of integer;
  function sum_sources (s : int_array) return integer;
  subtype summed is sum_sources integer;
end package;

package body summing is
  function sum_sources (s : int_array) return integer is
    variable total : integer := 0;
  begin
    for i in s'range loop
      total := total + s(i);
    end loop;
    return total;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.summing.all;

entity foreign_dormant is
  port (y : out std_logic; q : out std_logic; w : out summed := 10);
end entity;

architecture model of foreign_dormant is
  attribute foreign of model : architecture is "VHPI dormlib dorm";
begin
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.summing.all;

entity top is
end entity;

architecture t of top is
  signal y : std_logic;
  signal q : std_logic;
  signal w : summed := 10;
begin
  u1 : entity work.foreign_dormant port map (y => y, q => q, w => w);
  y <= '1';
  w <= 5;
end architecture;
