-- fsim-expect: FSIM-ELAB-VHDLCASE-011 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 10.9: case choices are locally static, not variables.
entity case_variable_choice is
end entity;

architecture a of case_variable_choice is
begin
  process
    variable k : integer := 1;
    variable p : integer := 5;
  begin
    case p is
      when k => null;
      when others => null;
    end case;
    wait;
  end process;
end architecture;
