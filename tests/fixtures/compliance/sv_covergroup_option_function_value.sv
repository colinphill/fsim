// fsim: top=sv_covergroup_option_function_value
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 19.7: an instance option assigned in the covergroup
// definition takes the value of its expression when the instance is
// constructed; the expression can call a function and reference the
// covergroup's formal arguments.
//   a = new(1): option.weight = twice(1) = 2, cp's at_least = twice(1) = 2.
//   b = new(3): option.weight = 6, at_least 2. a samples 0, 0, 1: only bin 0
//   reaches 2 hits, 1 of 4 bins (25%). b samples every value twice: 100%.
//   Type coverage (19.11.3): (2 * 25 + 6 * 100) / (2 + 6) = 81.25.
module sv_covergroup_option_function_value;
  function automatic int twice(int value);
    return 2 * value;
  endfunction
  covergroup cg(int w) with function sample(input bit [1:0] value);
    option.weight = twice(w);
    cp: coverpoint value {
      option.at_least = twice(1);
      bins values[] = {0, 1, 2, 3};
    }
  endgroup
  cg a = new(1);
  cg b = new(3);
  int a_weight, b_weight, at_least;
  real a_coverage, b_coverage, type_coverage;
  initial begin
    a.sample(0);
    a.sample(0);
    a.sample(1);
    for (int v = 0; v < 8; v++) b.sample(v % 4);
    a_weight = a.option.weight;
    b_weight = b.option.weight;
    at_least = a.cp.option.at_least;
    a_coverage = a.get_inst_coverage();
    b_coverage = b.get_inst_coverage();
    type_coverage = cg::get_coverage();
    if (a_weight == 2 && b_weight == 6 && at_least == 2 && a_coverage == 25.0
        && b_coverage == 100.0 && type_coverage == 81.25)
      $display("PASS");
    else
      $display("FAIL weights=%0d/%0d at_least=%0d a=%f b=%f type=%f",
        a_weight, b_weight, at_least, a_coverage, b_coverage, type_coverage);
  end
endmodule
