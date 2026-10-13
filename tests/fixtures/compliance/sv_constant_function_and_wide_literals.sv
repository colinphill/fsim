// fsim: top=sv_constant_function_and_wide_literals std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 5.7.1: an unsized based number grows to fit its digits;
// 13.4.3: constant functions evaluate local variables with their declared
// initialization values.
module sv_constant_function_and_wide_literals;
  function int func_const_kj_init();
    int result;
    for (int i = 0; i < 3; i++) begin
      automatic int kj;
      automatic int kj_init = 10;
      kj = kj + 1;
      kj_init = kj_init + 1;
      result = kj_init;
    end
    return result;
  endfunction

  localparam int FUNCTION_RESULT = func_const_kj_init();
  localparam NEGATIVE = -1;
  bit [35:0] extended;
  int failures;

  initial begin
    extended = NEGATIVE;
    if (extended != 36'hfffffffff) failures++;
    extended = 'd0 + NEGATIVE;
    if (extended != 36'h0ffffffff) failures++;
    if (FUNCTION_RESULT != 11) failures++;
    if ($bits('d123456789123456789123456789) != 87) failures++;
    if ($bits('h123456789123456789123456789) != 108) failures++;
    if ($bits('o123456777123456777123456777) != 81) failures++;
    if ($bits('b10101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010) != 92)
      failures++;
    if (failures == 0) $display("PASS");
    else $fatal(1, "FAIL %0d", failures);
  end
endmodule
