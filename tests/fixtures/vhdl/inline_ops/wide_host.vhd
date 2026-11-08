-- SPDX-License-Identifier: Apache-2.0
--
-- Reads and writes of 96 bit signals the host holds, from processes the v4
-- kernel compiles (they have sensitivity lists): src, dst, part and part3
-- each have a driver with a delayed assignment (IEEE 1076-2019 10.5.2.1),
-- which keeps the signal on the host's drivers. The compiled processes read
-- src whole, write dst whole, write a 72 bit slice of part and a slice of
-- part3 with a variable bound, all through the chunks of the register file
-- (no operation helper). src is x"0123456789abcdef01234567" from 1 ns and
-- x"89abcdef0123456789abcdef" from 3 ns; every compiled process runs when src
-- changes (and once at time 0, when src is zero).
--   dst <= src:                  dst = src                 (the delayed driver
--       of dst only drives 'Z', which loses to every value, 14.7.3.4)
--   part(87 downto 16) <= src(71 downto 0): the 72 low bits of src are the
--       18 low hexadecimal digits of src: 6789abcdef01234567, then
--       ef0123456789abcdef; the other 24 bits are only driven 'Z'
--   part3(i + 71 downto i) <= src(71 downto 0), i = 8: bits 79..8
-- to_hstring (IEEE 1076-2008 16.4) writes a nibble of 'Z' as Z.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture sim of top is
  signal src : std_logic_vector(95 downto 0) := (others => '0');
  signal dst : std_logic_vector(95 downto 0) := (others => 'Z');
  signal part : std_logic_vector(95 downto 0) := (others => 'Z');
  signal part3 : std_logic_vector(95 downto 0) := (others => 'Z');
  signal idx : integer := 8;
begin
  src <= x"0123456789abcdef01234567" after 1 ns, x"89abcdef0123456789abcdef" after 3 ns;
  dst <= (others => 'Z') after 1 ns;
  part <= (others => 'Z') after 1 ns;
  part3 <= (others => 'Z') after 1 ns;

  copy : process (src)
  begin
    dst <= src;
  end process;

  slice : process (src)
  begin
    part(87 downto 16) <= src(71 downto 0);
  end process;

  dynamic : process (src)
    variable i : integer;
  begin
    i := idx;
    part3(i + 71 downto i) <= src(71 downto 0);
  end process;

  reporter : process
  begin
    wait for 2 ns;
    report "wide dst=" & to_hstring(dst) & ";";
    report "wide part=" & to_hstring(part) & ";";
    report "wide part3=" & to_hstring(part3) & ";";
    wait for 2 ns;
    report "wide dst=" & to_hstring(dst) & ";";
    report "wide part=" & to_hstring(part) & ";";
    report "wide part3=" & to_hstring(part3) & ";";
    wait;
  end process;
end architecture;
