-- fsim: top=vhdl_assertion_sensitivity(a) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 11.5 and 10.2: a concurrent assertion is sensitive to the
-- longest static prefix of each signal name in its condition, so an event
-- on another element of the same signal does not resume it.
entity vhdl_assertion_sensitivity is
end entity;

architecture a of vhdl_assertion_sensitivity is
  signal v : bit_vector(1 to 3);
begin
  stimulus : process is
  begin
    wait for 1 ns;
    v(2) <= '1';
    wait for 1 ns;
    report "PASS";
    wait;
  end process;

  first_only : assert v(1) = '0' and now = 0 ns
    report "woken by another element" severity error;
end architecture;
