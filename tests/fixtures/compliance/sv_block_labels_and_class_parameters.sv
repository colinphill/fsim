// fsim: top=sv_block_labels_and_class_parameters
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 9.3.5: statement labels on sequential and parallel blocks,
// matched by their end labels; 8.25: a class whose parameter has no default
// is legal while it is not specialized.
module sv_block_labels_and_class_parameters;
  class box #(int N = 1);
    int value = N;
  endclass

  class unused #(int M);
  endclass

  box b;
  int failures = 0;
  int a, c;

  initial begin
    seq: begin
      a = 1;
    end: seq
    par: fork
      c = 2;
    join: par
    b = new;
    if (a != 1 || c != 2 || b.value != 1) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL count %0d", failures);
  end
endmodule
