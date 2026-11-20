-- fsim-expect: FSIM-ELAB-059 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 10.2: each name of a sensitivity list denotes a signal.
entity d7_wait_on_variable is
end entity;

architecture a of d7_wait_on_variable is
begin
  process
    variable v : bit := '0';
  begin
    wait on v;
    wait;
  end process;
end architecture;
