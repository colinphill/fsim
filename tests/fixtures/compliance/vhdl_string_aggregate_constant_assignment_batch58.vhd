-- fsim: top=vhdl_string_aggregate_constant_assignment_batch58(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
entity vhdl_string_aggregate_constant_assignment_batch58 is
end entity;

architecture sim of vhdl_string_aggregate_constant_assignment_batch58 is
  subtype text8 is string(1 to 8);
  constant aggregate_text : text8 :=
    ('A', 'B', 'C', 'D', 'E', 'F', 'G', 'H');
begin
  check_assignments : process
    variable direct_target : text8 := "12345678";
    variable formal_target : text8 := "12345678";
    variable initialized_target : text8 := aggregate_text;

    procedure copy_text(target : out text8) is
    begin
      target := aggregate_text;
    end procedure;
  begin
    direct_target := aggregate_text;
    assert direct_target = "ABCDEFGH"
      report "FAIL direct STRING assignment lost aggregate characters"
      severity failure;

    copy_text(formal_target);
    assert formal_target = "ABCDEFGH"
      report "FAIL procedure STRING assignment lost aggregate characters"
      severity failure;
    assert initialized_target = "ABCDEFGH"
      report "FAIL declaration STRING initialization changed"
      severity failure;

    assert false report "PASS" severity note;
    wait;
  end process;
end architecture;
