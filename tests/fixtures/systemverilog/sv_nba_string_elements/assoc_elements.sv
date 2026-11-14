// SPDX-License-Identifier: Apache-2.0
//
// A nonblocking assignment to an element of an associative array (IEEE
// 1800-2023 7.8, 10.4.2) captures the key and the value when the statement
// executes and writes the element in the NBA region: an existing key takes the
// new value, a key the array does not hold is created by the assignment, and
// of two assignments to one key the later one wins.
module top;
  int ai[int];
  initial begin
    ai[1] = 10;
    ai[1] <= 11;
    ai[5] <= 50;
    $display("sns assoc same-step n=%0d a1=%0d e5=%0d", ai.num(), ai[1], ai.exists(5));
    #1;
    $display("sns assoc next-step n=%0d a1=%0d a5=%0d e5=%0d", ai.num(), ai[1], ai[5], ai.exists(5));
    ai[2] <= 20;
    ai[2] <= 21;
    ai[7] <= #1 70;
    #1;
    $display("sns assoc mid n=%0d a2=%0d e7=%0d", ai.num(), ai[2], ai.exists(7));
    #1;
    $display("sns assoc last n=%0d a7=%0d", ai.num(), ai[7]);
  end
endmodule
