// fsim: top=sv_task_copyout_signedness_batch56 std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 13.3.3: task copy-out assigns from the formal's type.
module sv_task_copyout_signedness_batch56;
  logic signed [15:0] signed_actual;
  logic [15:0] unsigned_actual;
  bit [15:0] two_state_actual;
  task unsigned_output(output logic [7:0] value);
    value = 8'h80;
  endtask
  task signed_output(output logic signed [7:0] value);
    value = -8'sd2;
  endtask
  task unknown_output(output logic signed [7:0] value);
    value = 8'bx0z00111;
  endtask
  task change_value(inout logic signed [7:0] value);
    value = -8'sd3;
  endtask
  initial begin
    unsigned_output(signed_actual);
    if (signed_actual !== 16'h0080) $fatal(1, "unsigned output sign extended");
    signed_output(unsigned_actual);
    if (unsigned_actual !== 16'hfffe) $fatal(1, "signed output zero extended");
    unknown_output(unsigned_actual);
    if (unsigned_actual !== 16'bxxxxxxxxx0z00111) $fatal(1, "signed X extension");
    unknown_output(two_state_actual);
    if (two_state_actual !== 16'h0007) $fatal(1, "two-state output conversion");
    change_value(unsigned_actual);
    if (unsigned_actual !== 16'hfffd) $fatal(1, "inout output extension");
    $display("PASS");
  end
endmodule
