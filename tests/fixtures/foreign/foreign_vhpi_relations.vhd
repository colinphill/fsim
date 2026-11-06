-- SPDX-License-Identifier: Apache-2.0
-- Relations of the class diagrams (IEEE 1076-2019 19.4, 19.10, 19.12).
entity leaf is
  generic (n : integer := 1);
  port (i : in bit; o : out bit);
end entity;

architecture rtl of leaf is
begin
  o <= i;
end architecture;

entity top is
end entity;

architecture a of top is
  component leaf is
    port (i : in bit; o : out bit);
  end component;
  -- A configuration specification with a binding indication's port map
  -- (IEEE 1076-2019 7.3.2).
  for u1 : leaf use entity work.leaf(rtl) port map (i => i, o => o);
  subtype small is integer range 2 to 6;
  signal s : bit := '0';
  signal t : bit := '0';
  signal d : bit := '0';
  signal x : bit := '0';
  signal e : boolean := false;
  signal f : boolean := false;
  signal g : boolean := false;
  signal h : boolean := false;

  function twice (x : integer) return integer is
  begin
    return x * 2;
  end function;

  procedure drive (signal d : out bit; v : in bit) is
  begin
    d <= v;
  end procedure;
begin
  u1 : leaf port map (i => s, o => t);
  u2 : entity work.leaf generic map (n => 5) port map (i => s, o => open);

  p : process is
    variable r : integer := 3;
  begin
    -- Not a static call, so it keeps its function call object.
    r := twice(r);
    drive(s, '1');
    r := r + 1;
    -- A static call: the design simulates its folded value, and the static
    -- model still holds the call.
    r := twice(2);
    wait;
  end process p;

  q : process (s) is
  begin
    null;
  end process q;

  -- The implicit signals of s and t (IEEE 1076-2019 14.7.3.3).
  d <= s'delayed(2 ns);
  e <= s'quiet(3 ns);
  f <= s'stable(5 ns);
  x <= t'transaction;
  -- S'STABLE and S'QUIET with no time are BOOLEAN implicit signals too.
  g <= t'stable;
  h <= t'quiet;

  -- The design's own reading of the implicit signals in the cycle of the
  -- event on s: s'stable(5 ns) and s'quiet(3 ns) are FALSE in it
  -- (IEEE 1076-2019 14.7.4, 16.2.4).
  r : process (s) is
  begin
    report "s=" & bit'image(s) & " stable=" & boolean'image(s'stable(5 ns))
      & " quiet=" & boolean'image(s'quiet(3 ns));
  end process r;
end architecture;
