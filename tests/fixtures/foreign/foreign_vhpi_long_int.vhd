-- SPDX-License-Identifier: Apache-2.0
-- Compiled as VHDL-2019, where INTEGER is 64 bits wide (IEEE 1076-2019
-- 5.2.3.1). big starts at 2**40 + 5, outside the range of vhpiIntT; small
-- is an INTEGER whose value fits it, narrow a subtype whose range does.
entity top is
end entity;

architecture a of top is
  subtype small_range is integer range 0 to 100;
  type longs is array (0 to 2) of integer;
  signal big : integer := 2**40 + 5;
  signal small : integer := 7;
  signal narrow : small_range := 42;
  signal v : longs := (-1, 2**33, 9);
begin
  process begin
    wait for 5 ns;
    std.env.finish;
  end process;
end architecture;
