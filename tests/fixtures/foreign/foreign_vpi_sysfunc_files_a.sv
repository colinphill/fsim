// SPDX-License-Identifier: Apache-2.0
// Two files with a stateful system function call at the same line and
// column: a signed $random here, an unsigned $urandom in the other file.
// Each call keeps its own width, sign and value (IEEE 1800-2023 38.15).
module ma;
    int x;
    initial begin
        x = $random;
    end
endmodule

module top;
    ma u1 ();
    mb u2 ();
    md_a u3 ();
    md_b u4 ();
endmodule
