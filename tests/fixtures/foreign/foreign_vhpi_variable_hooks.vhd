-- SPDX-License-Identifier: Apache-2.0
-- Callbacks on variables of a running process (IEEE 1076-2019 21.3.2.2,
-- 22.5.2): a value-change callback keeps the variable watched until the last
-- callback on it is removed; a callback registered or a deposit made while the
-- process runs takes effect at once, in the statements that follow it. The
-- process is the first of the design.
entity top is
end entity;

architecture a of top is
  signal clk : bit := '0';
  signal sink : integer := 0;
begin
  p : process (clk) is
    variable a : integer := 0;
    variable b : integer := 0;
    variable c : integer := 0;
    variable r : integer := 0;
  begin
    if clk = '1' then
      for i in 1 to 2 loop
        a := a + 1;
        b := b + 1;
        r := r + b;
        c := a + b;
      end loop;
    end if;
    sink <= r + c;
  end process;

  clk <= not clk after 1 ns when now < 10 ns;
end architecture;
