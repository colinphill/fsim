-- fsim: top=vhdl_scalar_logic_chain_batch58(test) std=1993
-- SPDX-License-Identifier: Apache-2.0
library ieee;
use ieee.std_logic_1164.all;

entity vhdl_scalar_logic_chain_batch58 is
end entity;

architecture test of vhdl_scalar_logic_chain_batch58 is
  signal real_sum, imag_sum : std_ulogic_vector(21 downto 17) := "00000";
  signal real_flag, imag_flag : std_ulogic := '0';
  signal overflow : std_ulogic;
begin
  overflow <= real_flag or imag_flag
    or (real_sum(21) xor real_sum(20))
    or (real_sum(21) xor real_sum(19))
    or (real_sum(21) xor real_sum(18))
    or (real_sum(21) xor real_sum(17))
    or (imag_sum(21) xor imag_sum(20))
    or (imag_sum(21) xor imag_sum(19))
    or (imag_sum(21) xor imag_sum(18))
    or (imag_sum(21) xor imag_sum(17)) after 3 ns;

  exercise : process
  begin
    wait for 4 ns;
    assert overflow = '0' severity failure;
    real_sum <= "10000";
    wait for 4 ns;
    assert overflow = '1' severity failure;
    real_sum <= "11111";
    wait for 4 ns;
    assert overflow = '0' severity failure;
    real_flag <= '1';
    wait for 4 ns;
    assert overflow = '1' severity failure;
    real_flag <= '0';
    imag_sum <= "01111";
    wait for 4 ns;
    assert overflow = '1' severity failure;
    report "PASS" severity note;
    wait;
  end process;
end architecture;
