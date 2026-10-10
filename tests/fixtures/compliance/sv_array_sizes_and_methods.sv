// fsim: top=sv_array_sizes_and_methods
// IEEE 1800-2017 7.4.2: an unpacked dimension given by a constant size
// expression; 7.8.1: wildcard associative-array indices; 7.12 and 13.4.1:
// built-in array methods called without parentheses; 14.4: `#1step`;
// 20.8.1: $clog2 of a run-time value; 8.25: a parameter of a class
// specialization read through a handle.
module sv_array_sizes_and_methods;
  timeunit 1ns; timeprecision 1ns;
  localparam N = 3;
  localparam W = 2;

  class holder #(parameter int depth = 12);
  endclass

  logic [7:0] stages[N + 1];
  int counts[2 ** W];
  int sparse[*];
  int values[];
  holder #(34) h;
  int failures = 0;
  integer x;

  initial begin
    foreach (stages[i]) stages[i] = i;
    if ($size(stages) != 4 || stages[3] != 3) failures++;
    counts[3] = 5;
    if ($size(counts) != 4 || counts[3] != 5) failures++;
    sparse[5] = 1;
    sparse[1000] = 2;
    if (sparse.num() != 2 || sparse[1000] != 2) failures++;
    values = new[3];
    values[0] = 3; values[1] = 1; values[2] = 2;
    values.sort;
    if (values[0] != 1 || values[2] != 3) failures++;
    values.reverse;
    if (values[0] != 3) failures++;
    values.delete;
    if (values.size() != 0) failures++;
    x = 1025;
    if ($clog2(x) != 11) failures++;
    x = 0;
    if ($clog2(x) != 0) failures++;
    h = new;
    if (h.depth != 34) failures++;
    #1step;
    if ($time != 1) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL count %0d", failures);
  end
endmodule
