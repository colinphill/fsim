// SPDX-License-Identifier: Apache-2.0
//
// A nonblocking assignment to an element of a container declared automatic in
// a procedural block is the automatic-variable error, reported at elaboration
// (IEEE 1800-2023 6.21, 10.4.2); the same element of a static container of the
// block is legal.
module top;
  initial begin
    automatic int aq[$];
    automatic string am[string];
    static int sq[$];
    aq.push_back(1);
    aq[0] <= 4;
    am["k"] <= "v";
    sq.push_back(1);
    sq[0] <= 3;
  end
endmodule
