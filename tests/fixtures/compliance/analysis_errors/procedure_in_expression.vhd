-- fsim-expect: FSIM-ELAB-VHCALL-004 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 4.2: a procedure call is a statement, never an expression.
entity procedure_in_expression is
end entity;

architecture a of procedure_in_expression is
  procedure bump(variable v : inout integer) is
  begin
    v := v + 1;
  end procedure;
begin
  process
    variable n : integer := 0;
  begin
    case bump(n) is
      when 0 => null;
      when others => null;
    end case;
    wait;
  end process;
end architecture;
