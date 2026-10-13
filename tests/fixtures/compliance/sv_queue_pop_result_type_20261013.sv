// fsim: top=sv_queue_pop_result_type std=2017
// SPDX-License-Identifier: Apache-2.0
module sv_queue_pop_result_type;
  shortint signed_values[$];
  bit [15:0] unsigned_values[$];
  string formatted;
  int result_value;
  longint size_value;
  initial begin
    signed_values = '{-1, -2, -3, -4, -5};
    unsigned_values = '{16'hffff, 16'hfffe, 16'hfffd};
    if ({signed_values.pop_front()} !== 16'hffff)
      $fatal(1, "pop result lost its self-determined width");
    if (signed_values.pop_front() >= 0 || unsigned_values.pop_front() <= 0)
      $fatal(1, "pop result lost its element signedness");
    formatted = $sformatf("%0d %0d", signed_values.pop_front(), unsigned_values.pop_front());
    if (formatted != "-3 65534")
      $fatal(1, "pop result format type differs");
    result_value = signed_values.pop_front() + 1;
    if (result_value != -3)
      $fatal(1, "signed arithmetic on pop result differs");
    result_value = signed_values.pop_front() + 32'd1;
    if (result_value != 65532)
      $fatal(1, "unsigned arithmetic context sign-extended a pop result");
    if (signed_values.size() != 0 || unsigned_values.size() != 1)
      $fatal(1, "type inspection executed a pop");
    size_value = result_value ? unsigned_values.size() : 64'h123;
    if (size_value != 1 || unsigned_values.size() < -1)
      $fatal(1, "size result must be a signed int extended in context");
    $display("PASS");
  end
endmodule
