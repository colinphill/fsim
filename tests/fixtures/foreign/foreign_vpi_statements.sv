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
endmodule
