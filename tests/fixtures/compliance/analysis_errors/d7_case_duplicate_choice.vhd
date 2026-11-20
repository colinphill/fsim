-- fsim-expect: FSIM-ELAB-VHDLCASE-003 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 10.9: each value of the selector type is represented once in the choices.
entity d7_case_duplicate_choice is
end entity;

architecture a of d7_case_duplicate_choice is
begin
  process
    type month is (jan, feb, mar);
    variable m : month := jan;
  begin
    case m is
      when jan => null;
      when feb | jan => null;
      when others => null;
    end case;
    wait;
  end process;
end architecture;
