-- SPDX-License-Identifier: Apache-2.0
-- Processes and drivers made by vhpi_create across a reset (IEEE 1076-2019
-- 20.9, 23.6): the VHPI program that registers library resetlib, model
-- summer, implements the foreign architecture and asks for the reset.
entity summer is
  port (y : out bit);
end entity;

architecture model of summer is
  attribute foreign of model : architecture is "VHPI resetlib summer";
  -- A user-defined resolution function that sums its sources, so each
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
begin
end architecture;

entity top is
end entity;

architecture t of top is
  signal y : bit;
begin
  u1 : entity work.summer port map (y => y);
end architecture;
