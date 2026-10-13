// fsim: top=sv_bounded_queue_function_conversion std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 7.10.5: a bounded queue keeps its first elements when a
// larger queue value is assigned to it.
module sv_bounded_queue_function_conversion;
  typedef int bounded_queue_t[$:1];

  function automatic bounded_queue_t make_bounded_queue();
    return '{1, 2, 3};
  endfunction

  int result[$];
  int smaller[$:0];

  initial begin
    result = make_bounded_queue();
    if (result.size() != 2 || result[0] != 1 || result[1] != 2) begin
      $fatal(1, "bounded queue conversion produced the wrong elements");
    end
    smaller = make_bounded_queue();
    if (smaller.size() != 1 || smaller[0] != 1) begin
      $fatal(1, "bounded queue assignment did not truncate the function result");
    end
    $display("PASS");
  end
endmodule
