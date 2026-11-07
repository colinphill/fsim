// SPDX-License-Identifier: Apache-2.0
// A delayed continuous assignment to a concatenation of net selects writes
// each part with the assignment's rise/fall delay (IEEE 1800-2023 10.3.3);
// a reader of the net wakes once for the parts that change in one time step.
module delayed_concat_select;
  logic x, y, p, q;
  wire [3:0] a;
  wire [1:0] b;
  integer wakes_a, wakes_b;

  assign #4 {a[3], a[1]} = {x, y};
  assign a[2] = 1'b0;
  assign a[0] = 1'b0;
  assign #(2, 3) {b[1], b[0]} = {p, q};

  always @(a) wakes_a = wakes_a + 1;
  always @(b) wakes_b = wakes_b + 1;

  initial begin
    x = 0; y = 0; p = 0; q = 0;
    wakes_a = 0; wakes_b = 0;
    #5 $display("t=5 a=%b b=%b", a, b);
    wakes_a = 0; wakes_b = 0;
    x = 1; y = 1; p = 1; q = 1;
    #1 $display("t=6 a=%b b=%b", a, b);
    #2 $display("t=8 a=%b b=%b", a, b);
    #2 $display("t=10 a=%b b=%b", a, b);
    p = 0; q = 0;
    #2 $display("t=12 a=%b b=%b", a, b);
    #4 $display("t=16 a=%b b=%b wakes_a=%0d wakes_b=%0d",
        a, b, wakes_a, wakes_b);
    $finish;
  end
endmodule
