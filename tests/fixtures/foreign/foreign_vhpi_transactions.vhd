-- SPDX-License-Identifier: Apache-2.0
-- Transactions of a driver (IEEE 1076-2019 10.5.2.2, 14.7.2, 19.12.1,
-- Figure 48): the process `drv` schedules waveforms at time zero, so after the
-- initialization phase each driver holds the transactions below.
--   s  (bit):             '1' at 10 ns, '0' at 20 ns
--   v(1) (bit_vector):    '1' at  5 ns, '0' at 15 ns
--   v(0):                 '0' at  5 ns, '1' at 15 ns
--   n  (integer):         7 at 3 ns
--   q  (bit):             no assignment after the initial value, no driver
--                         transaction is pending
entity top is
end entity;

architecture rtl of top is
  signal s : bit := '0';
  signal v : bit_vector(1 downto 0) := "00";
  signal n : integer := 0;
  signal q : bit := '0';
begin
  drv : process is
  begin
    s <= '1' after 10 ns, '0' after 20 ns;
    v <= "10" after 5 ns, "01" after 15 ns;
    n <= 7 after 3 ns;
    wait;
  end process;

  qdrv : process is
  begin
    q <= '0';
    wait;
  end process;
end architecture;
