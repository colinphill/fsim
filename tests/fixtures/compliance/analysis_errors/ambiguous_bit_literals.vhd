-- fsim-expect: FSIM-ELAB-VHOPER-003 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 9.2.3: '0' is both a BIT and a CHARACTER literal, so the
-- type of the comparison operands is ambiguous.
entity ambiguous_bit_literals is
end entity;

architecture a of ambiguous_bit_literals is
begin
  process
    variable k : integer := 0;
  begin
    if '0' = '0' then
      k := 5;
    end if;
    wait;
  end process;
end architecture;
