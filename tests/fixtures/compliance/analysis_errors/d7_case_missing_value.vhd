-- fsim-expect: FSIM-ELAB-VHDLCASE-005 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 10.9: without OTHERS the choices cover every value of the selector subtype.
entity d7_case_missing_value is
end entity;

architecture a of d7_case_missing_value is
begin
  process
    type level is (low, mid, high);
    variable l : level := low;
  begin
    case l is
      when low => null;
      when high => null;
    end case;
    wait;
  end process;
end architecture;
