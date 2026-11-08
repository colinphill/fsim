// SPDX-License-Identifier: Apache-2.0
// The value of a user function call is what the call returned at its last
// evaluation (IEEE 1800-2023 37.42, 38.15). Evaluated twice, with n = 3 and
// then n = 4; the plug-in reads the calls before the first evaluation and
// at the end of the run.
//   a = twice(n)         n = 4: 8
//   b = twice(a) + 1     the operation combines the call's value: 8 * 2 + 1 = 17
//   p = bump(n)          n = 4 truncated to the 8-bit formal, + 1: 5
//   h = half(n)          n = 4 converted to real, / 2.0: 2
module top;
    int a, b;
    logic [7:0] p;
    real h;

    function automatic int twice(input int x);
        return x * 2;
    endfunction

    function automatic logic [7:0] bump(input logic [7:0] v);
        return v + 8'd1;
    endfunction

    function automatic real half(input real r);
        return r / 2.0;
    endfunction

    task automatic calls(input int n);
        a = twice(n);
        b = twice(a) + 1;
        p = bump(n);
        h = half(n);
    endtask

    initial begin
        calls(3);
        #1 calls(4);
    end
endmodule
