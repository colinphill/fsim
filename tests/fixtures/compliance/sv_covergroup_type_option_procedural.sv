// fsim: top=sv_covergroup_type_option_procedural
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 19.7.1: the type options other than strobe and
// real_interval can be set procedurally at any time, as
// cg::type_option.m and cg::item::type_option.m; they belong to the
// covergroup type, not to an instance.
//   With type_option.merge_instances = 1, the type coverage is that of the
//   merged instance (19.11.3), whose coverpoints weigh their
//   type_option.weight. g covers 4 of 4 bins of a (100%) and 2 of 4 of b
//   (50%): (1 * 100 + 1 * 50) / 2 = 75; after cg::b::type_option.weight = 3:
//   (1 * 100 + 3 * 50) / 4 = 62.5. The instance's coverage keeps its own
//   option.weight values: (100 + 50) / 2 = 75. A type goal of 60 is read
//   back and does not scale the coverage (19.11).
module sv_covergroup_type_option_procedural;
  covergroup cg with function sample(input bit [1:0] x, input bit [1:0] y);
    type_option.merge_instances = 1;
    a: coverpoint x { bins v[] = {0, 1, 2, 3}; }
    b: coverpoint y { bins v[] = {0, 1, 2, 3}; }
  endgroup
  cg g = new;
  real before_value, after, instance_after;
  int b_weight, g_b_weight, goal, group_weight;
  string comment;
  initial begin
    g.sample(0, 0);
    g.sample(1, 1);
    g.sample(2, 0);
    g.sample(3, 1);
    before_value = cg::get_coverage();
    cg::b::type_option.weight = 3;
    cg::type_option.goal = 60;
    cg::type_option.weight = 4;
    cg::type_option.comment = "merged";
    after = cg::get_coverage();
    instance_after = g.get_inst_coverage();
    b_weight = cg::b::type_option.weight;
    g_b_weight = g.b.option.weight;
    goal = cg::type_option.goal;
    group_weight = cg::type_option.weight;
    comment = cg::type_option.comment;
    if (before_value == 75.0 && after == 62.5 && instance_after == 75.0
        && b_weight == 3 && g_b_weight == 1 && goal == 60
        && group_weight == 4 && comment == "merged")
      $display("PASS");
    else
      $display("FAIL before_value=%f after=%f inst=%f b=%0d/%0d goal=%0d weight=%0d comment=%s",
        before_value, after, instance_after, b_weight, g_b_weight, goal,
        group_weight, comment);
  end
endmodule
