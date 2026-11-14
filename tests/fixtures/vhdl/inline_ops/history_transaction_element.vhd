-- SPDX-License-Identifier: Apache-2.0
--
-- S(i)'TRANSACTION (IEEE 1076-2019 16.2.4, 14.7.4, 14.7.5.2): the implicit
-- signal of an element toggles in the update phase of the cycle in which
-- that element is active, and only then; S'TRANSACTION of the whole signal
-- toggles whenever any element is active. The checks compare each
-- attribute with its value read before the wait, so they do not depend on
-- the attribute's initial value (16.2.4).
--   v (bit_vector(1 downto 0), "00"):
--     1 ns  v(0) <= '0'   an unchanged value: a transaction on element 0
--     2 ns  v(1) <= '1'   element 1 only
--     3 ns  v(0) <= '1'   element 0 only
--     4 ns  v <= "11"     a transaction on both elements (v(1) unchanged)
--   The observer waits on all three attributes. After each wait it reports
--   which of them toggled relative to the value read before the wait.
entity top is
end entity;

architecture rtl of top is
  signal v : bit_vector(1 downto 0) := "00";
begin
  drive: process
  begin
    wait for 1 ns;
    v(0) <= '0';
    wait for 1 ns;
    v(1) <= '1';
    wait for 1 ns;
    v(0) <= '1';
    wait for 1 ns;
    v <= "11";
    wait;
  end process;

  observe: process
    variable p0, p1, pw : bit;
  begin
    loop
      p0 := v(0)'transaction;
      p1 := v(1)'transaction;
      pw := v'transaction;
      wait on v(0)'transaction, v(1)'transaction, v'transaction;
      report "wide el t0=" & boolean'image(v(0)'transaction /= p0)
        & " t1=" & boolean'image(v(1)'transaction /= p1)
        & " tw=" & boolean'image(v'transaction /= pw)
        & " at " & time'image(now);
    end loop;
  end process;
end architecture;
