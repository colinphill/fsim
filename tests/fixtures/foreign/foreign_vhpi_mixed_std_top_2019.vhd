-- SPDX-License-Identifier: Apache-2.0
-- Compiled as VHDL-2019 over the VHDL-2008 leaf. src is 2**31-1, so the
-- leaf's q = p + 1 is 2**31 = 2147483648, which only a 64-bit INTEGER holds.
-- word is a user subtype written with the 32-bit bounds: the 64-bit INTEGER
-- does not widen it, so w holds word'high = 2**31-1 and wl word'low = -2**31.
entity top is
end entity;

architecture a of top is
  signal src : integer := 2147483647;
  signal dst : integer := 0;
  subtype word is integer range -2147483648 to 2147483647;
  signal w : word := word'high;
  signal wl : integer := word'low;
begin
  u : entity work.leaf port map (p => src, q => dst);

  process begin
    wait for 5 ns;
    std.env.finish;
  end process;
end architecture;
