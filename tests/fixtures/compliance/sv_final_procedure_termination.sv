// fsim: top=sv_final_procedure_termination
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 9.2.3: a final procedure may call $finish or $stop, which
// end the simulation at once; here the check that would call $stop passes.
module sv_final_procedure_termination;
  int count = 0;

  initial begin
    repeat (3) #1 count++;
    $finish;
  end

  final begin
    if (count != 3) $stop;
    if (count == 3) $display("PASS");
    else $display("FAIL count %0d", count);
  end
endmodule
