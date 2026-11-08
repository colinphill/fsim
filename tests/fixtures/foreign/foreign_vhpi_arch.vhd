-- SPDX-License-Identifier: Apache-2.0
-- A foreign architecture (IEEE 1076-2019 20.2.4.2): the VHPI program that
-- registers library archlib, model buf, implements it.
library ieee;
use ieee.std_logic_1164.all;

entity foreign_buf is
  port (a : in bit; y : out bit; v : out bit_vector(1 downto 0));
end entity;

architecture model of foreign_buf is
  attribute foreign of model : architecture is "VHPI archlib buf";
  signal state : bit := '0';
  -- Resolved: the two created processes and a driver of no process are
  -- three sources (IEEE 1076-2019 23.6, 14.7.3.2).
  signal r : std_logic;
  -- A user-defined resolution function: it sums its sources, so each
  -- source's value, the driver default 10 included, is visible (14.7.2,
  -- 14.7.3.2).
  type int_array is array (natural range <>) of integer;
  function sum_sources (s : int_array) return integer is
    variable total : integer := 0;
  begin
    for i in s'range loop
      total := total + s(i);
    end loop;
    return total;
  end function;
  subtype summed is sum_sources integer;
  signal w : summed := 10;
  -- A composite of a resolved element subtype: each element is resolved on
  -- its own by the user function (14.7.3.2).
  type summed_pair is array (0 to 1) of summed;
  signal p : summed_pair := (10, 20);
  -- A record resolved as a whole by a function whose parameter is an array of
  -- the record type (4.6): lo is the sum of the sources' lo and hi the OR of
  -- their hi. Every source, created ones included, starts at the signal's
  -- default (1, "01") (14.7.2).
  type pair is record
    lo : integer;
    hi : bit_vector(1 downto 0);
  end record;
  type pair_array is array (natural range <>) of pair;
  function merge_pairs (s : pair_array) return pair is
    variable result : pair := (0, "00");
  begin
    for i in s'range loop
      result.lo := result.lo + s(i).lo;
      result.hi := result.hi or s(i).hi;
    end loop;
    return result;
  end function;
  subtype merged is merge_pairs pair;
  signal rr : merged := (1, "01");
begin
end architecture;

entity top is
end entity;

architecture t of top is
  signal a : bit := '0';
  signal y : bit;
  signal v : bit_vector(1 downto 0);
  signal z : bit;
begin
  u1 : entity work.foreign_buf port map (a => a, y => y, v => v);
  -- The foreign model's output drives the surrounding VHDL.
  z <= y;
  stim : process
  begin
    wait for 10 ns;
    a <= '1';
    wait for 10 ns;
    a <= '0';
    wait;
  end process;
end architecture;
