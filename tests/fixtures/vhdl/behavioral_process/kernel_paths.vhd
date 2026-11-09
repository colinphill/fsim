-- SPDX-License-Identifier: Apache-2.0
--
-- Kernel paths of VHDL behavioral processes (IEEE 1076-2019 11.3) that the
-- v4 kernel once took wrongly. The expected reports (kernel_paths.expected)
-- are derived by hand:
--   sliced formals (6.5.6.3, 14.7.3): kernel_paths_sub drives o <= not i
--   after 1 ns, and its formals are associated by slices. A matured delayed
--   transaction on o reaches the actuals c and d in the same update phase:
--   c = d = "1111" from 1 ns; a <= "11" at 2 ns gives c = "1100" from 3 ns;
--   b <= "011110" at 4 ns makes i = "01111011", o = "10000100", so c =
--   "0100" and d = "1000" from 5 ns. The monitor samples at 0, 2, 4, 6 ns.
--   wide element store (10.5.2.2): an element of an array of 65-bit vectors
--   selected by a variable index takes all ones after a delta; its neighbours
--   keep their zeros.
--   real operands (IEEE 1076.2 math_real): SIGN returns 0.0, 1.0 or -1.0
--   exactly, so every comparison with the expected sign is exact (60).
--   postponed process (11.3, 14.7.5.3): x takes 1, 2 and 3 in the deltas of
--   1 ns and 4 at 2 ns. The postponed process resumes once at the end of each
--   time step (x = 3 at 1 ns, x = 4 at 2 ns), then assigns y <= 2 after 1 ns,
--   which wakes the waiting process at 3 ns.
library ieee;
use ieee.math_real.all;

entity kernel_paths_sub is
  port (
    i : in bit_vector(7 downto 0);
    o : out bit_vector(7 downto 0));
end entity kernel_paths_sub;

architecture rtl of kernel_paths_sub is
begin
  o <= not i after 1 ns;
end architecture rtl;

library ieee;
use ieee.math_real.all;

entity kernel_paths is
end entity kernel_paths;

architecture tb of kernel_paths is
  type wide_array is array (0 to 7) of bit_vector(1 to 65);
  type real_vector is array (positive range <>) of real;
  type integer_vector is array (positive range <>) of integer;

  signal a : bit_vector(1 downto 0);
  signal b : bit_vector(5 downto 0);
  signal c : bit_vector(5 downto 2);
  signal d : bit_vector(3 downto 0);
  signal wide : wide_array;
  signal x, y : integer;

  function digits(expected : real; result : real) return integer is
    variable scale : real;
  begin
    if expected = result then
      return 60;
    end if;
    if expected = 0.0 then
      scale := 1.0;
    else
      scale := 10.0 ** integer(floor(log10(abs(expected))));
    end if;
    return integer(ceil(-log10(abs((expected - result) / scale))));
  end function digits;
begin
  sub : entity work.kernel_paths_sub
    port map (
      i(1 downto 0) => a,
      i(7 downto 2) => b,
      o(3 downto 0) => c,
      o(7 downto 4) => d);

  slices : process
  begin
    for step in 0 to 3 loop
      report "c=" & to_string(c) & " d=" & to_string(d)
        & " @" & integer'image(now / 1 ns) & " ns";
      if step = 1 then
        a <= "11";
      elsif step = 2 then
        b <= "011110";
      end if;
      wait for 2 ns;
    end loop;
    wait;
  end process slices;

  store : process
    variable index : integer := 2;
  begin
    wide(index) <= (others => '1');
    wait for 1 ns;
    report "wide(2)=" & to_string(wide(2)(1 to 2)) & ".." & to_string(wide(2)(64 to 65))
      & " wide(1)=" & to_string(wide(1)(64 to 65))
      & " wide(3)=" & to_string(wide(3)(1 to 2));
    wait;
  end process store;

  signs : process
    constant arg : real_vector := (0.0, 1.0, -1.0, 4.5, -4.7, 1.0e38);
    constant expected : real_vector(1 to arg'length) := (0.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    variable results : real_vector(1 to arg'length);
    variable accuracy : integer_vector(1 to arg'length);
  begin
    wait for 1 ns;
    for k in 1 to arg'length loop
      results(k) := sign(arg(k));
    end loop;
    for k in 1 to arg'length loop
      accuracy(k) := digits(expected(k), results(k));
      report "sign " & integer'image(k) & " digits=" & integer'image(accuracy(k));
    end loop;
    wait;
  end process signs;

  writer : process
  begin
    wait for 1 ns;
    x <= 1;
    wait for 0 ns;
    x <= 2;
    wait for 0 ns;
    x <= 3;
    wait for 1 ns;
    x <= 4;
    wait on y;
    report "y=" & integer'image(y) & " @" & integer'image(now / 1 ns) & " ns";
    wait;
  end process writer;

  late : postponed process
  begin
    wait on x;
    report "postponed x=" & integer'image(x) & " @" & integer'image(now / 1 ns) & " ns";
    wait on x;
    report "postponed x=" & integer'image(x) & " @" & integer'image(now / 1 ns) & " ns";
    y <= 2 after 1 ns;
    wait;
  end process late;
end architecture tb;
