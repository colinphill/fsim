-- fsim: top=matching_units_and_bindings(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 9.2.3: matching ordering operators of STD_ULOGIC and
-- BIT; 5.2.4.1: a physical unit name alone denotes one unit; 6.5.6.2:
-- REAL generics with static actuals; 6.5.7.1: a port actual with a
-- conversion function of an array-of-INTEGER formal; 4.2.1: a generic
-- function declared with its generic clause after the designator; 7.3.2:
-- a binding indication whose generic map supplies an entity generic that
-- the component does not declare.
library ieee;
use ieee.std_logic_1164.all;

package matching_units_pkg is
  type triple is array (0 to 2) of natural;
  function spread(value : integer) return triple;
end package;

package body matching_units_pkg is
  function spread(value : integer) return triple is
  begin
    return (value, value + 1, value + 2);
  end function;
end package body;

use work.matching_units_pkg.all;

entity matching_units_child is
  generic (scale : real; count : integer);
  port (values : in triple; total : out integer);
end entity;

architecture rtl of matching_units_child is
begin
  total <= integer(scale * real(values(0) + values(1) + values(2))) + count;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.matching_units_pkg.all;

entity matching_units_and_bindings is
end entity;

architecture sim of matching_units_and_bindings is
  type distance is range 0 to 1000000
    units
      inch;
      ft = 12 inch;
      yd = 3 ft;
    end units;

  function adder generic (n : integer) (x : integer) return integer is
  begin
    return x + n;
  end function;
  function add2 is new adder generic map (2);

  component counter_like
    port (values : in triple; total : out integer);
  end component;
  for bound : counter_like use entity work.matching_units_child(rtl)
    generic map (scale => 1.0, count => 3);

  signal seed : integer := 4;
  signal direct_total, bound_total : integer;
begin
  direct : entity work.matching_units_child
    generic map (scale => real(2), count => 1)
    port map (values => spread(seed), total => direct_total);
  bound : counter_like port map (values => spread(seed), total => bound_total);

  check : process
    variable failures : natural := 0;
    variable span : distance;
    variable lt, le, ge, gt, ordered : std_ulogic;
    variable bit_ge : bit;
  begin
    lt := '0' ?< '1';
    le := '1' ?<= 'L';
    ge := '1' ?>= 'U';
    gt := 'H' ?> 'X';
    ordered := '1' ?> 'L';
    if lt /= '1' or le /= '0' or ge /= 'U' or gt /= 'X'
        or ordered /= '1' then
      failures := failures + 1;
    end if;
    bit_ge := bit'('1') ?>= bit'('0');
    if bit_ge /= '1' then
      failures := failures + 1;
    end if;
    span := 2 * yd;
    if span /= 72 * inch or yd /= 3 * ft then
      failures := failures + 1;
    end if;
    if add2(5) /= 7 then
      failures := failures + 1;
    end if;
    wait for 1 ns;
    -- 4 + 5 + 6 = 15: twice that plus 1, and once that plus 3.
    if direct_total /= 31 or bound_total /= 18 then
      failures := failures + 1;
    end if;
    if failures = 0 then
      report "PASS";
    else
      report "FAIL count " & integer'image(failures) severity error;
    end if;
    wait;
  end process;
end architecture;
