// SPDX-License-Identifier: Apache-2.0
// Processes, statements and expressions seen through VPI (IEEE 1800-2023
// 37.43-37.79).
module top;
    logic clk = 1'b0;
    logic [3:0] q;
    logic [3:0] d;
    wire [3:0] w;
    integer i;

    assign #2 w = d ^ 4'b1010;

    always @(posedge clk) q <= d;

    always_comb begin
        if (d[0])
            i = 1;
        else
            i = 2;
        case (d[2:1])
            2'b00: i = 3;
            default: i = 4;
        endcase
    end

    task automatic pulse(input integer n);
        repeat (n) #1 clk = ~clk;
    endtask

    initial begin : run
        d = 4'hc;
        for (int k = 0; k < 2; k++)
            d = d + 1;
        pulse(2);
        #5 $display("done %0d", q);
    end

    logic [3:0] r;
    initial begin
        #10 force q = 4'h5;
        #2 release q;
        #1 assign r = d;
        #1 deassign r;
    end

    // Increments are operations, compound updates assignments with an
    // operator, and output tasks keep their source name and format
    // (37.60-37.63, 37.79).
    integer n;
    initial begin
        n = 0;
        n++;
        --n;
        n += 3;
        $displayh("n=%0h", n);
        $write("plain");
    end

    // The range and typespec of a return value (37.41).
    function automatic logic [7:0] wide(input logic [3:0] a);
        return {a, a};
    endfunction

    // A net declaration assignment is a continuous assignment of its own
    // (6.7.1, 37.46); the declaration's delay is its delay.
    wire [3:0] #3 nw = d | 4'b0001;

    // Immediate assertions: assert, assume or cover, observed deferred by #0
    // or final deferred, whose actions are subroutine calls (16.4, 37.55).
    task automatic chk;
        assert (d !== 4'bzzzz) n = 1;
        assume #0 (d !== 4'bzzzz) else $display("assume failed");
        cover final (d !== 4'bzzzz) $display("covered");
    endtask

    // A foreach statement keeps its collection, its loop variables (a
    // skipped one is a null operation) and its body (12.7.3, 37.51).
    logic [3:0] arr [0:2][0:1];
    task automatic each;
        foreach (arr[i, ])
            n = i;
    endtask

    // A foreach over a string (12.7.3, SystemVerilog-2023) is a foreach too:
    // collection, one loop variable and its body.
    task automatic each_str;
        string str;
        foreach (str[k])
            n = k;
    endtask

    // Operation expressions have the value of their operands combined by the
    // operator, self-determined (11.4, 11.6.1, 11.8.1, 37.78). Expected
    // binary values, widths from 11.6.1:
    //   P * 3 + M        32-bit signed: 18 + -4 = 14
    //   U + 4'd7         4-bit unsigned: 10 + 7 = 17 modulo 16 = 1
    //   (P > M) ? P - M : 0   P > M is 1, P - M = 10
    //   P / M            6 / -4 truncates toward zero: -1 (11.4.2)
    //   P % M            the sign of the first operand: 2 (11.4.2)
    //   ~U               4-bit: ~1010 = 0101
    //   &U               reduction and of 1010 = 0
    //   {U, U}           8 bits: 10101010
    //   U << 2           the left operand's 4 bits: 1000
    //   -P               32-bit two's complement of 6
    localparam int P = 6;
    localparam int M = -4;
    localparam logic [3:0] U = 4'ha;
    task automatic ops;
        n = P * 3 + M;
        n = U + 4'd7;
        n = (P > M) ? P - M : 0;
        n = P / M;
        n = P % M;
        n = ~U;
        n = &U;
        n = {U, U};
        n = U << 2;
        n = -P;
        n = {2{U}}; // 10101010 (11.4.12.1)
        n = {2{U, 2'b01}}; // 101001101001
    endtask

    // Output task arguments as written (37.62): a lone literal, and string
    // literals after the first argument, are arguments too; a string literal
    // is decompiled with its escapes.
    task automatic shown;
        $display(5);
        $write("a\n", n, "b");
    endtask

    // The file forms keep their arguments as written too: the descriptor,
    // the format, each value and string literal, and an empty argument as
    // a null operation (21.2.1, 21.3.2, 37.42 detail 8, 37.62).
    task automatic shown_file;
        $fdisplay(n, "v=%0d", n, "w");
        $fwrite(n, , n);
    endtask

    // A for loop that declares its variable is a scope, the scope of that
    // variable; the loop's uses of it are the variable (12.7.1, 37.12
    // detail 2).
    task automatic counted;
        for (byte unsigned j = 0; j < 3; j++)
            n = j;
    endtask
endmodule
