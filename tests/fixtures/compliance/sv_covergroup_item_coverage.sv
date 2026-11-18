// fsim: top=sv_covergroup_item_coverage
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 19.9, 19.11, 19.11.3: get_inst_coverage() of a coverpoint
// or cross is its coverage in one instance; get_coverage() of a coverpoint or
// cross, called through an instance or as cg::item::get_coverage(), is its
// type coverage.
//   g1 samples (0,0) and (1,1): a covers 2 of 2 bins (100%), b 2 of 4 (50%),
//   ab 2 of 2 * 4 = 8 (25%). g2 samples (0,2): a 50%, b 25%, ab 12.5%.
//   Unmerged (19.11.3), an item's type coverage is its coverage in each
//   instance weighted by its option.weight there: a (100 + 50) / 2 = 75,
//   b (50 + 25) / 2 = 37.5, ab (25 + 12.5) / 2 = 18.75. With g2.a's
//   option.weight 3: a (1 * 100 + 3 * 50) / 4 = 62.5.
//   Merged (type_option.merge_instances = 1), an item's type coverage is its
//   coverage in the union of the instances: m1 and m2 sample as g1 and g2,
//   so ab covers (0,0), (1,1) and (0,2), 3 of 8: 37.5; a 100%.
module sv_covergroup_item_coverage;
  covergroup cg with function sample(input bit [1:0] x, input bit [1:0] y);
    a: coverpoint x { bins lo = {0}; bins hi = {1}; }
    b: coverpoint y { bins v[] = {0, 1, 2, 3}; }
    ab: cross a, b;
  endgroup
  covergroup cm with function sample(input bit [1:0] x, input bit [1:0] y);
    type_option.merge_instances = 1;
    a: coverpoint x { bins lo = {0}; bins hi = {1}; }
    b: coverpoint y { bins v[] = {0, 1, 2, 3}; }
    ab: cross a, b;
  endgroup
  cg g1 = new;
  cg g2 = new;
  cm m1 = new;
  cm m2 = new;
  real g1a, g1b, g1ab, g2a, g2ab;
  real ta, tb, tab, ta_inst, ta_weighted, ma, mab;
  initial begin
    g1.sample(0, 0);
    g1.sample(1, 1);
    g2.sample(0, 2);
    m1.sample(0, 0);
    m1.sample(1, 1);
    m2.sample(0, 2);
    g1a = g1.a.get_inst_coverage();
    g1b = g1.b.get_inst_coverage();
    g1ab = g1.ab.get_inst_coverage();
    g2a = g2.a.get_inst_coverage();
    g2ab = g2.ab.get_inst_coverage();
    ta = cg::a::get_coverage();
    tb = cg::b::get_coverage();
    tab = cg::ab::get_coverage();
    ta_inst = g1.a.get_coverage();
    g2.a.option.weight = 3;
    ta_weighted = cg::a::get_coverage();
    ma = cm::a::get_coverage();
    mab = m1.ab.get_coverage();
    if (g1a == 100.0 && g1b == 50.0 && g1ab == 25.0 && g2a == 50.0
        && g2ab == 12.5 && ta == 75.0 && tb == 37.5 && tab == 18.75
        && ta_inst == 75.0 && ta_weighted == 62.5 && ma == 100.0
        && mab == 37.5)
      $display("PASS");
    else
      $display("FAIL g1=%f/%f/%f g2=%f/%f type=%f/%f/%f/%f/%f merged=%f/%f",
        g1a, g1b, g1ab, g2a, g2ab, ta, tb, tab, ta_inst, ta_weighted, ma,
        mab);
  end
endmodule
