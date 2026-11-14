-- SPDX-License-Identifier: Apache-2.0
--
-- Guarded std_logic signals (IEEE 1076-2019 6.4.2.3): a null transaction
-- (10.5.2.2) disconnects a driver, which then is no source of the signal
-- (14.7.3.2). With no connected source a bus element takes the resolution
-- function's value for a null array ('Z' for std_logic_1164's resolved) and
-- a register element keeps its value. A guarded assignment's false guard
-- schedules the null transaction after the disconnection delay (7.4: 1 ns
-- for b, 0 ns by default), 11.6; a value transaction reconnects the driver.
--   b (bus) and r (register), each driven by blocks blk1 (guard g1, value
--   v1) and blk2 (guard g2, value v2), both guards false at first:
--     initial b = r = resolved("UU") = 'U'; r's drivers disconnect in the
--     first delta (r keeps 'U'), b's at 1 ns (b 'Z').
--     a  5 ns: b 'Z' r 'U'
--     b 10 ns (g1, v1 '1' at 5 ns):   b '1' r '1'
--     c 15 ns (g2, v2 '0' at 10 ns):  b 'X' r 'X'
--     d 20 ns (g1 false at 15 ns):    b '0' (at 16 ns) r '0'
--     e 25 ns (g2 false at 20 ns):    b 'Z' (at 21 ns) r keeps '0'
--   bd (bus): blkd1 (guard gd) drives vd, blkd2 (guard false) '-'; one
--     connected '-' source gives '-' (resolved of a one-element array).
--     f 26 ns (vd '-', gd at 25 ns):  bd '-'
--   q (std_logic_vector bus): process seqp drives all of q, block blk3
--     (guard g3) q(1 downto 0) <= guarded "0Z"; seqp assigns null itself.
--     0 ns: "UUUU", then "1010" (blk3 disconnected in the first delta)
--     30 ns g3: q(1) = resolved('1', '0') 'X', q(0) = resolved('0', 'Z')
--       '0': "10X0"
--     32 ns seqp's null (at 31 ns, after 1 ns): q(3 downto 2) have no
--       source: "ZZ0Z"
--     33 ns g3 false: "ZZZZ"
--     35 ns and 36 ns: seqp's "0110" after 1 ns, null after 2 ns
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture a of top is
  signal b : std_logic bus;
  signal r : std_logic register;
  signal g1, g2 : boolean := false;
  signal v1, v2 : std_logic := '0';
  disconnect b : std_logic after 1 ns;
  signal bd : std_logic bus;
  signal gd : boolean := false;
  signal vd : std_logic := '0';
  signal q : std_logic_vector(3 downto 0) bus;
  signal g3 : boolean := false;
begin
  blk1 : block (g1)
  begin
    b <= guarded v1;
    r <= guarded v1;
  end block;

  blk2 : block (g2)
  begin
    b <= guarded v2;
    r <= guarded v2;
  end block;

  blkd1 : block (gd)
  begin
    bd <= guarded vd;
  end block;

  blkd2 : block (false)
  begin
    bd <= guarded '-';
  end block;

  blk3 : block (g3)
  begin
    q(1 downto 0) <= guarded "0Z";
  end block;

  seqp : process
  begin
    q <= "1010";
    wait for 31 ns;
    q <= null after 1 ns;
    wait for 3 ns;
    q <= "0110" after 1 ns, null after 2 ns;
    wait;
  end process;

  monb : process (b)
  begin
    report "wide b=" & std_logic'image(b) & " at " & integer'image(now / 1 ns)
      & ";";
  end process;

  monq : process (q)
  begin
    report "wide q=" & to_string(q) & " at " & integer'image(now / 1 ns) & ";";
  end process;

  stim : process
    procedure show (name : string) is
    begin
      report "wide " & name & " b=" & std_logic'image(b) & " r="
        & std_logic'image(r) & " bd=" & std_logic'image(bd) & ";";
    end procedure;
  begin
    wait for 5 ns;
    show("a");
    g1 <= true;
    v1 <= '1';
    wait for 5 ns;
    show("b");
    g2 <= true;
    v2 <= '0';
    wait for 5 ns;
    show("c");
    g1 <= false;
    wait for 5 ns;
    show("d");
    g2 <= false;
    wait for 5 ns;
    show("e");
    vd <= '-';
    gd <= true;
    wait for 1 ns;
    show("f");
    wait for 4 ns;
    g3 <= true;
    wait for 3 ns;
    g3 <= false;
    wait;
  end process;
end architecture;
