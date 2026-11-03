// SPDX-License-Identifier: Apache-2.0
// Drivers and loads, var selects and task and function declarations seen
// through VPI (IEEE 1800-2023 37.21, 37.41, 37.42, 37.46, 37.58, 37.74).
module sub (input logic a, output logic y);
    assign y = ~a;
endmodule

module top;
    logic [3:0] mem [0:3];
    logic [3:0] d;
    logic [3:0] q;
    wire [3:0] w;
    logic s_in;
    wire s_out;
    integer idx;

    assign w = d + 4'd1;

    sub u (.a(s_in), .y(s_out));

    function automatic int add(input int x, input int y);
        return x + y;
    endfunction

    function logic [7:0] wide(input logic [3:0] n);
        return {n, n};
    endfunction

    task automatic bump(input integer n, output integer m);
        m = n + 1;
    endtask

    initial begin
        d = 4'h3;
        mem[1] = d;
        idx = 2;
        mem[idx] = mem[1] + 1;
        q = mem[2];
        for (int k = 0; k < 2; k++)
            q = q + 1;
        bump(1, idx);
        $display("%0d", add(1, 2));
    end

    // A generate block and another top-level module name d by a simple and
    // by a hierarchical name (37.21, 37.46); system function calls (37.42).
    logic gv;
    if (1) begin : gen
        assign gv = d[0];
    end

    integer sv, tv;
    initial begin
        sv = $clog2(8);
        tv = $time;
    end
endmodule

module spy;
    logic z;
    assign z = top.d[1];
    wire [1:0] nd = top.d[3:2];
endmodule
