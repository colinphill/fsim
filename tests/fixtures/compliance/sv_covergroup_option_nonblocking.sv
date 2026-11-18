// fsim: top=sv_covergroup_option_nonblocking
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 10.4.2, 4.4.2.6, 19.7, 19.7.1: a nonblocking assignment to
// a covergroup option evaluates its value when the statement executes and
// sets the option in the NBA region, after the active and inactive regions
// of the time slot.
//   heavy covers 2 of 4 bins (50%), light all 4 (100%). Before the NBA
//   region both weigh 1: type coverage (50 + 100) / 2 = 75 (19.11.3). After
//   heavy.option.weight <= 3: (3 * 50 + 1 * 100) / 4 = 62.5. The type goal
//   written nonblocking reads 100 until the NBA region, then 50, and does
//   not scale the coverage (19.11).
module sv_covergroup_option_nonblocking;
  covergroup cg with function sample(input bit [1:0] value);
    value_point: coverpoint value {
      bins values[] = {0, 1, 2, 3};
    }
  endgroup
  cg heavy = new;
  cg light = new;
  int weight_value;
  int before_value, inactive, after;
  int goal_before, goal_after;
  real early, late;
  initial begin
    heavy.sample(0);
    heavy.sample(1);
    for (int v = 0; v < 4; v++) light.sample(v);
    weight_value = 3;
    heavy.option.weight <= weight_value;
    cg::type_option.goal <= 50;
    weight_value = 7;
    before_value = heavy.option.weight;
    goal_before = cg::type_option.goal;
    early = cg::get_coverage();
    #0;
    inactive = heavy.option.weight;
    #1;
    after = heavy.option.weight;
    goal_after = cg::type_option.goal;
    late = cg::get_coverage();
    if (before_value == 1 && inactive == 1 && after == 3 && goal_before == 100
        && goal_after == 50 && early == 75.0 && late == 62.5)
      $display("PASS");
    else
      $display("FAIL before_value=%0d inactive=%0d after=%0d goal=%0d/%0d early=%f late=%f",
        before_value, inactive, after, goal_before, goal_after, early, late);
  end
endmodule
