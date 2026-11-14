// SPDX-License-Identifier: Apache-2.0
//
// A nonblocking assignment to a numeric element of a string-keyed associative
// array (IEEE 1800-2023 7.8.1, 10.4.2) samples the key and the value when the
// statement executes and writes the element in the NBA region: an existing key
// takes the new value, a key the array does not hold is created by the
// assignment, of two assignments to one key the later one wins, and an
// intra-assignment delay moves the write to the later time slot.
module top;
  int m[string];
  logic [7:0] lm[string];
  string k;
  initial begin
    m["a"] = 10;
    m["a"] <= 11;
    m["b"] <= 50;
    $display("sns key same-step n=%0d a=%0d eb=%0d", m.num(), m["a"], m.exists("b"));
    #1;
    $display("sns key next-step n=%0d a=%0d b=%0d eb=%0d", m.num(), m["a"], m["b"], m.exists("b"));
    m["c"] <= 20;
    m["c"] <= 21;
    m["d"] <= #1 70;
    #1;
    $display("sns key mid n=%0d c=%0d ed=%0d", m.num(), m["c"], m.exists("d"));
    #1;
    $display("sns key last n=%0d d=%0d", m.num(), m["d"]);
    // The value and the key are those of the statement's execution.
    m["e"] = 1;
    m["e"] <= m["e"] + 5;
    m["e"] = 100;
    k = "x";
    m[k] <= 7;
    k = "y";
    m[k] <= 8;
    $display("sns key sampled e=%0d n=%0d", m["e"], m.num());
    #1;
    $display("sns key sampled-next e=%0d x=%0d y=%0d n=%0d", m["e"], m["x"], m["y"], m.num());
    lm["p"] <= 8'hA5;
    lm["p"] <= 8'h5A;
    lm["q"] <= 8'bxz01_xxxx;
    #1;
    $display("sns key logic p=%h q=%b n=%0d", lm["p"], lm["q"], lm.num());
  end
endmodule
