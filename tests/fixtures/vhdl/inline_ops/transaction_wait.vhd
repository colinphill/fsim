-- SPDX-License-Identifier: Apache-2.0
--
-- A process waiting on S'TRANSACTION resumes in every simulation cycle in
-- which S is active, whether or not the assignment changes its value (IEEE
-- 1076-2019 16.2.4: S'TRANSACTION toggles each time a transaction occurs on
-- S; 14.7.3.1: an assignment of the value a driver already has is a
-- transaction; 10.2: a wait resumes on an event on any signal it names).
--   s (bit, '0'): '0' at 1 ns (a transaction without an event), '1' at
--     2 ns (an event), '1' at 3 ns and '0' at 4 ns. The waiter resumes at
--     1, 2, 3 and 4 ns.
--   v (2 bits, "00"): "00" at 5 ns (no event) and "01" at 6 ns. The waiter
--     on the vector's transactions resumes at 5 and 6 ns.
entity top is
end entity;

architecture rtl of top is
  signal s : bit := '0';
  signal v : bit_vector(1 downto 0) := "00";
begin
  drive: process
  begin
    wait for 1 ns;
    s <= '0';
    wait for 1 ns;
    s <= '1';
    wait for 1 ns;
    s <= '1';
    wait for 1 ns;
    s <= '0';
    wait for 1 ns;
    v <= "00";
    wait for 1 ns;
    v <= "01";
    wait;
  end process;

  wait_s: process
  begin
    wait on s'transaction;
    report "wide tw s at " & time'image(now);
  end process;

  wait_v: process
  begin
    wait on v'transaction;
    report "wide tw v at " & time'image(now);
  end process;

end architecture;
