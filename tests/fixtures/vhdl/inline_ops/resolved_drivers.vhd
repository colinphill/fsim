-- SPDX-License-Identifier: Apache-2.0
--
-- A std_logic signal that several VHDL processes drive is resolved by the
-- v4 kernel inline, in generated code: each driver process writes its own
-- contribution and the signal's value is the resolved_function of
-- IEEE 1076-2019 14.7.3.2 over the drivers of each element, by the
-- resolution table of std_logic_1164 (IEEE 1164, "resolution_table"), for
-- a scalar, a vector and a vector of more than 64 elements, and for all
-- 81 pairs of values. The members are sensitive to clk, so the kernel
-- compiles them; none of their writes goes to the host (the profile shows
-- no host store). Drivers start at the signal's default value (14.7.2) and
-- the signal's initial value is the resolution of the drivers' initial
-- values (14.7.5.2): an initial '-' of two drivers reads 'X' at time 0.
-- A driver that has no assignment to an element does not take part in the
-- resolution of the element; a single driver keeps its value ('-' too).
--
-- cnt counts the rising edges of clk (5 ns, 15 ns, ...); the reports are at
-- the falling edges, so report n shows what the drivers assigned at the
-- rising edge with cnt = n - 1.
--   cnt 0  p1 '0'  "01ZH"  1s with w(66) 'Z'     p2 'Z'  "ZZ01"  'Z's
--          s: 0,Z = 0   v: 0Z 1Z Z0 H1 = 0 1 0 1   w: 1,Z = 1; Z,Z = Z
--   cnt 1  p1 'Z'  "ZZZZ"  'Z's                  p2 '1'  "1010"  '0's
--          s: 1   v: 1010   w: 0s
--   cnt 2  p1 'H'  "HHLL"  'H's                  p2 'L'  "LH-U"  'L's
--          s: H,L = W   v: HL HH L- LU = W H X U   w: H,L = W
--   cnt 3+ p1 assigns only s ('1'); v and w keep p1's "HHLL" and 'H's;
--          p2 'Z'  "ZZZZ"  'W's
--          s: 1,Z = 1   v: HHLL,ZZZZ = HHLL   w: H,W = W
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture sim of top is
  signal clk : std_logic := '0';
  signal cnt : integer := 0;
  signal s : std_logic;
  signal v : std_logic_vector(3 downto 0);
  signal w : std_logic_vector(69 downto 0);
  signal t : std_logic_vector(80 downto 0);
  signal d : std_logic := '-';
begin
  clkgen : process
  begin
    for k in 1 to 10 loop
      wait for 5 ns;
      clk <= not clk;
    end loop;
    wait;
  end process;

  count : process (clk)
  begin
    if rising_edge(clk) then
      cnt <= cnt + 1;
    end if;
  end process;

  p1 : process (clk)
  begin
    if rising_edge(clk) then
      d <= '-';
      case cnt is
        when 0 =>
          s <= '0';
          v <= "01ZH";
          w <= (others => '1');
          w(66) <= 'Z';
          t <= "UUUUUUUUUXXXXXXXXX000000000111111111ZZZZZZZZZWWWWWWWWWLLLLLLLLLHHHHHHHHH---------";
        when 1 =>
          s <= 'Z';
          v <= "ZZZZ";
          w <= (others => 'Z');
        when 2 =>
          s <= 'H';
          v <= "HHLL";
          w <= (others => 'H');
        when others =>
          s <= '1';
      end case;
    end if;
  end process;

  p2 : process (clk)
  begin
    if rising_edge(clk) then
      d <= '-';
      case cnt is
        when 0 =>
          s <= 'Z';
          v <= "ZZ01";
          w <= (others => 'Z');
          t <= "UX01ZWLH-UX01ZWLH-UX01ZWLH-UX01ZWLH-UX01ZWLH-UX01ZWLH-UX01ZWLH-UX01ZWLH-UX01ZWLH-";
        when 1 =>
          s <= '1';
          v <= "1010";
          w <= (others => '0');
        when 2 =>
          s <= 'L';
          v <= "LH-U";
          w <= (others => 'L');
        when others =>
          s <= 'Z';
          v <= "ZZZZ";
          w <= (others => 'W');
      end case;
    end if;
  end process;

  mon : process (clk)
  begin
    if falling_edge(clk) then
      report "wide " & integer'image(cnt) & " s=" & to_string(s)
        & " v=" & to_string(v) & " w=" & to_string(w) & ";";
      if cnt = 1 then
        report "wide table=" & to_string(t) & ";";
      end if;
    end if;
  end process;

  -- Time zero, after initialization: d reads the resolution of its two
  -- drivers' initial values ('-' and '-' resolve to 'X'), and s, v and w
  -- the resolution of their drivers' 'U'.
  init : process
  begin
    report "wide init d=" & to_string(d) & " s=" & to_string(s) & " v="
      & to_string(v) & ";";
    wait;
  end process;
end architecture;
