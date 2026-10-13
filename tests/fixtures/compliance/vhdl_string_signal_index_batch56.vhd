-- fsim: top=vhdl_string_signal_index_batch56(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- Packed STRING signals preserve CHARACTER values and declared index direction.
entity vhdl_string_signal_index_batch56 is
end entity;
architecture sim of vhdl_string_signal_index_batch56 is
  signal ascending_value : string(4 to 6) := "A Z";
  signal descending_value : string(9 downto 7) := "Q R";
begin
  process
    variable index_value : integer := 5;
  begin
    assert ascending_value(4) = 'A' and ascending_value(index_value) = ' ' and
           ascending_value(6) = 'Z'
      report "ascending STRING signal index" severity failure;
    index_value := 8;
    assert descending_value(9) = 'Q' and descending_value(index_value) = ' ' and
           descending_value(7) = 'R'
      report "descending STRING signal index" severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
