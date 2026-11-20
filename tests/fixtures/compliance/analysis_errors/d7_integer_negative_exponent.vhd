-- fsim-expect: FSIM-ELAB-091 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.2.8: an integer left operand of ** takes a nonnegative exponent.
entity d7_integer_negative_exponent is
end entity;

architecture a of d7_integer_negative_exponent is
begin
  process
    variable k : integer := 10 ** (-2);
  begin
    wait;
  end process;
end architecture;
