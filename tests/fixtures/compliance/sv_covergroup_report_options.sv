// fsim: top=sv_covergroup_report_options
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 19.7 Table 19-1: option.comment, option.detect_overlap and
// option.cross_num_print_missing of a covergroup, coverpoint and cross are
// accepted and read back. detect_overlap set on bins that do not overlap
// raises no warning; cross_num_print_missing does not change coverage.
//   x covers 2 of its 3 bins: px coverage 2/3 = 66.67% (19.11.1), so
//   between 66 and 67.
module sv_covergroup_report_options;
  covergroup cg with function sample(input bit [2:0] x, input bit y);
    option.comment = "report group";
    px: coverpoint x {
      option.detect_overlap = 1;
      option.comment = "x values";
      bins low = {[0:1]};
      bins mid = {[2:3]};
      bins high = {[4:7]};
    }
    py: coverpoint y;
    xy: cross px, py {
      option.cross_num_print_missing = 2;
    }
  endgroup
  cg g = new;
  string group_comment, point_comment;
  int overlap, missing;
  real coverage;
  initial begin
    g.sample(0, 0);
    g.sample(2, 1);
    group_comment = g.option.comment;
    point_comment = g.px.option.comment;
    overlap = g.px.option.detect_overlap;
    missing = g.xy.option.cross_num_print_missing;
    coverage = g.px.get_inst_coverage();
    if (group_comment == "report group" && point_comment == "x values"
        && overlap == 1 && missing == 2 && coverage > 66.0 && coverage < 67.0)
      $display("PASS");
    else
      $display("FAIL comments=%s/%s overlap=%0d missing=%0d coverage=%f",
        group_comment, point_comment, overlap, missing, coverage);
  end
endmodule
