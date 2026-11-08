-- SPDX-License-Identifier: Apache-2.0
-- Compiled as VHDL-2008 with no VHDL-2019 unit in the design: INTEGER stays
-- 32 bits. src is 2**31-2, so the leaf's q = p + 1 is 2**31-1 = 2147483647,
-- the largest INTEGER.
entity top is
end entity;

architecture a of top is
  signal src : integer := 2147483646;
  signal dst : integer := 0;
begin
  u : entity work.leaf port map (p => src, q => dst);

  process begin
    wait for 5 ns;
    std.env.finish;
  end process;
end architecture;
