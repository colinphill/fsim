-- SPDX-License-Identifier: Apache-2.0
-- The kernel's bookkeeping for 'LAST_VALUE adds no signal a foreign
-- interface can see (IEEE 1076-2019 16.2.4, 23.2): a design reading
-- 'LAST_VALUE of two signals lists exactly its declared signals, whatever
-- they are called, and a name that would belong to a hidden signal denotes
-- nothing, or the user's own extended identifier signal.
entity top is
end entity;

architecture rtl of top is
  signal s : bit := '0';
  signal v : bit_vector(1 downto 0) := "00";
  signal \s'last_value\ : bit := '1';
begin
  drive: process
  begin
    wait for 1 ns;
    s <= '1';
    v <= "01";
    wait for 1 ns;
    s <= '0';
    wait;
  end process;

  probe: process
  begin
    wait for 3 ns;
    report "lv s=" & bit'image(s'last_value) & " v=" & to_string(v'last_value)
      & " user=" & bit'image(\s'last_value\);
    wait;
  end process;
end architecture;
