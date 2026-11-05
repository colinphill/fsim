-- SPDX-License-Identifier: Apache-2.0
-- Relations of the class diagrams (IEEE 1076-2019 19.4, 19.10, 19.12).
entity leaf is
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
  subtype small is integer range 2 to 6;
  signal s : bit := '0';
  signal t : bit := '0';

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
  u2 : entity work.leaf port map (i => s, o => open);

  p : process is
    variable r : integer := 3;
  begin
    -- Not a static call, so it keeps its function call object (#141 covers
    -- folded static calls).
    r := twice(r);
    drive(s, '1');
    r := r + 1;
    wait;
  end process p;

  q : process (s) is
  begin
    null;
  end process q;
end architecture;
