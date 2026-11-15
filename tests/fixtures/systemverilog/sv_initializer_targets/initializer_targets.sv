// SPDX-License-Identifier: Apache-2.0
// Static variable declaration initializers that are not elaboration
// constants run once, before any procedure (IEEE 1800-2023 6.8, 6.21), and
// are initialization, not transactions: a plain variable, an output variable
// port whose net has a second driver (a resolved target), and unpacked
// structure and array variables (aggregate targets). No procedure sees an
// event from them and no VHDL process sees a transaction.
package initializer_values;
  function automatic logic one();
    return 1'b1;
  endfunction
  function automatic int five();
    return 5;
  endfunction
endpackage

module initializer_driver(output logic o = initializer_values::one());
endmodule

module top;
  timeunit 1ns;
  typedef struct { int a; int b; } pair_t;
  wire n;
  assign n = 1'bz;
  initializer_driver d(.o(n));
  logic p = initializer_values::one();
  pair_t s = '{a: initializer_values::five(), b: 4};
  int arr[2] = '{initializer_values::five(), 6};
  real r = 1.5 * initializer_values::five();
  initializer_leaf u(.r(n), .p(p));
  always @(d.o) $display("sit o event %b at %0t", d.o, $time);
  always @(p) $display("sit p event %b at %0t", p, $time);
  always @(s.a) $display("sit s event %0d at %0t", s.a, $time);
  always @(arr[0]) $display("sit arr event %0d at %0t", arr[0], $time);
  always @(r) $display("sit r event %0.1f at %0t", r, $time);
  initial begin
    $display("sit t0 n=%b o=%b p=%b s=%0d,%0d arr=%0d,%0d r=%0.1f",
      n, d.o, p, s.a, s.b, arr[0], arr[1], r);
    #2;
    s.a = 8;
    arr[0] = 7;
    #1 $finish;
  end
endmodule
