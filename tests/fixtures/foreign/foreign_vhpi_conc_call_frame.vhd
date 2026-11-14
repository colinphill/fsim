-- SPDX-License-Identifier: Apache-2.0
-- The frame of a concurrent procedure call (IEEE 1076-2019 11.4, 14.6 b, 19.10):
-- the call is equivalent to a process that contains it, so its call object has
-- a frame, with the variables of each activation, which a deposit reaches.
entity top is end entity;
architecture rtl of top is
  signal a : integer := 0;
  signal o : integer := 0;
  procedure scale(signal x : in integer; signal y : out integer) is
    variable k : integer := 3;
  begin
    k := k + 1;
    y <= x * k;
  end procedure;
begin
  a <= 1, 2 after 2 ns, 3 after 4 ns;
  scale(a, o);
end architecture;
