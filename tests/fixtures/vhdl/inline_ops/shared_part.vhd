-- SPDX-License-Identifier: Apache-2.0
--
-- Part-select and element assignments, with variable bounds or index, to
-- VHDL-93 shared variables (IEEE 1076-1993 4.3.1.3: a shared variable is
-- updated at once, not at the end of the delta) from a process the v4 kernel
-- compiles (it has a sensitivity list), stored in place with no operation
-- helper: sv is std_logic_vector (nine values, four planes in the kernel),
-- sb is bit_vector (two planes). When trig becomes '1' at 1 ns the process
-- runs with i = n = 2:
--   sv(i + 3 downto i) := "1HZW"   sv(5)='1', sv(4)='H', sv(3)='Z', sv(2)='W'
--   sv(i + 1) := 'L'               sv(3)='L'
--   sb(i + 3 downto i) := "1011"   sb(5)=1 sb(4)=0 sb(3)=1 sb(2)=1
--   sb(i + 4) := '1'               sb(6)=1
-- sv starts as all 'U' and sb as all '0', so, from element 7 down to 0, at 2 ns:
--   sv = U U 1 H L W U U      sb = 0 1 1 0 1 1 0 0
-- A second run, at 3 ns (trig2), writes sv(i + 9 downto i + 2) := "10ZWLHXU"
-- with i = n = 2, so sv(11 downto 4) = 1 0 Z W L H X U of a 16 element
-- variable sv2 that starts 'U': from element 15 down to 0
--   sv2 = U U U U 1 0 Z W L H X U U U U U
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture sim of top is
  shared variable sv : std_logic_vector(7 downto 0) := "UUUUUUUU";
  shared variable sb : bit_vector(7 downto 0) := "00000000";
  shared variable sv2 : std_logic_vector(15 downto 0) := (others => 'U');
  signal trig : std_logic := '0';
  signal trig2 : std_logic := '0';
  signal n : integer := 2;

  function image(v : std_logic_vector) return string is
    constant chars : string(1 to 9) := "UX01ZWLH-";
    variable r : string(1 to v'length);
    variable k : integer := 1;
  begin
    for j in v'range loop
      r(k) := chars(std_logic'pos(v(j)) + 1);
      k := k + 1;
    end loop;
    return r;
  end function;

  function image(v : bit_vector) return string is
    variable r : string(1 to v'length);
    variable k : integer := 1;
  begin
    for j in v'range loop
      if v(j) = '1' then
        r(k) := '1';
      else
        r(k) := '0';
      end if;
      k := k + 1;
    end loop;
    return r;
  end function;
begin
  trig <= '1' after 1 ns;
  trig2 <= '1' after 2500 ps;

  writer : process (trig)
    variable i : integer;
  begin
    if trig = '1' then
      i := n;
      sv(i + 3 downto i) := "1HZW";
      sv(i + 1) := 'L';
      sb(i + 3 downto i) := "1011";
      sb(i + 4) := '1';
    end if;
  end process;

  writer2 : process (trig2)
    variable i : integer;
  begin
    if trig2 = '1' then
      i := n;
      sv2(i + 9 downto i + 2) := "10ZWLHXU";
    end if;
  end process;

  reporter : process
  begin
    wait for 2 ns;
    report "shared sv=" & image(sv) & ";";
    report "shared sb=" & image(sb) & ";";
    wait for 1 ns;
    report "shared sv2=" & image(sv2) & ";";
    wait;
  end process;
end architecture;
