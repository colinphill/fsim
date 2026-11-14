-- SPDX-License-Identifier: Apache-2.0
--
-- Signals of resolved subtypes the design declares over STD_ULOGIC (IEEE
-- 1076-2019 4.6), every driver a process: the kernel keeps their nine
-- states, holds each driver's value and runs the resolution function over
-- the drivers' STD_ULOGIC values, in the order of their processes, once in
-- each update phase that changes a driver (14.7.3.2).
--   pulled: an open-drain bus with a pull-up. A driver at '0' or 'L' pulls
--     it to '0', 'Z' and 'H' release it ('H' when all do), and any other
--     value gives 'X', which stays.
--   o (pulled, p1 and p2): 'Z' and 'H' give 'H'; at 1 ns p1's 'L' gives
--     '0'; at 2 ns p1's 'H' gives 'H'; at 3 ns p1's 'W' gives 'X'.
--   ov (array of pulled elements, each resolved on its own, 14.7.3.2):
--     element 1 has p1's 'Z' and p2's '0', so '0'; element 0 two 'Z', so
--     'H': "0H". At 3 ns p1's "1Z" makes element 1 'X' ('1' is neither
--     pulled nor released): "XH".
--   a (anded: the STD_LOGIC_1164 "and" of the drivers, from "1111"):
--     "1111" and "1100" is "1100"; at 1 ns p1's "1H0Z" gives "110X" and
--     then "1100" (X and 0 is 0); at 2 ns p2's "-111" gives "110X" and
--     "-111", "X10X" ('-' and 1 is X).
-- mon samples between the changes: at 0.5, 1.5, 2.5 and 3.5 ns.
library ieee;
use ieee.std_logic_1164.all;

package r9 is
  function pulled (v : std_ulogic_vector) return std_ulogic;
  subtype od_logic is pulled std_ulogic;
  type od_vector is array (natural range <>) of od_logic;
  subtype nibble is std_ulogic_vector(3 downto 0);
  type nibble_array is array (natural range <>) of nibble;
  function anded (v : nibble_array) return nibble;
  subtype and4 is anded nibble;
end package;

package body r9 is
  function pulled (v : std_ulogic_vector) return std_ulogic is
    variable r : std_ulogic := 'H';
  begin
    for i in v'range loop
      case v(i) is
        when '0' | 'L' =>
          if r /= 'X' then
            r := '0';
          end if;
        when 'Z' | 'H' =>
          null;
        when others =>
          r := 'X';
      end case;
    end loop;
    return r;
  end function;

  function anded (v : nibble_array) return nibble is
    variable r : nibble := "1111";
  begin
    for i in v'range loop
      r := r and v(i);
    end loop;
    return r;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.r9.all;

entity top is
end entity;

architecture a of top is
  signal o : od_logic;
  signal ov : od_vector(1 downto 0);
  signal an : and4;
begin
  p1 : process
  begin
    o <= 'Z';
    ov <= "ZZ";
    an <= "1111";
    wait for 1 ns;
    o <= 'L';
    an <= "1H0Z";
    wait for 1 ns;
    o <= 'H';
    wait for 1 ns;
    o <= 'W';
    ov <= "1Z";
    wait;
  end process;

  p2 : process
  begin
    o <= 'H';
    ov <= "0Z";
    an <= "1100";
    wait for 2 ns;
    an <= "-111";
    wait;
  end process;

  mon : process
  begin
    wait for 500 ps;
    for step in 0 to 3 loop
      report "wide o=" & std_ulogic'image(o) & " ov=" & std_ulogic'image(ov(1))
        & std_ulogic'image(ov(0)) & " an=" & std_ulogic'image(an(3))
        & std_ulogic'image(an(2)) & std_ulogic'image(an(1))
        & std_ulogic'image(an(0)) & " at " & integer'image(step) & ".5";
      wait for 1 ns;
    end loop;
    wait;
  end process;
end architecture;
