-- SPDX-License-Identifier: Apache-2.0
-- Local loads of implicit signals (IEEE 1076-2019 16.2.4, 19.12.2.3): a
-- process that reads S'STABLE(T), S'DELAYED(T) or S'QUIET(T), the
-- equivalent process of a guarded signal assignment, which reads GUARD
-- (11.6), and the port an implicit signal is associated with as an actual;
-- reading an implicit signal does not read its prefix.
entity leaf is
  port (i : in bit; j : in boolean);
end entity;

architecture rtl of leaf is
begin
end architecture;

entity top is
end entity;

architecture rtl of top is
  signal s, t, w, q, r : bit;
begin
  p1 : process (s'stable(1 ns)) is
  begin
    t <= s'delayed(2 ns);
  end process;

  p2 : process is
  begin
    wait until s'quiet;
  end process;

  p3 : process (r) is
  begin
    w <= r;
  end process;

  b : block (s = '1') is
  begin
    q <= guarded s;
  end block;

  u : entity work.leaf port map (i => s'delayed(3 ns), j => s'stable);

  s <= '1' after 5 ns;
end architecture;
