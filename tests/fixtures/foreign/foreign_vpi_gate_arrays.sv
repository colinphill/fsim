// SPDX-License-Identifier: Apache-2.0
// Gate instance arrays whose bounds are parameters (IEEE 1800-2023 28.3.6,
// 37.35): an ascending and a descending array in a gcell that the top module
// instantiates with the default bounds and with overridden ones. The arrays
// are expanded at elaboration, once the bounds are known; their elements are
// primitives of the gcell, in the order of the range.
module gcell #(parameter int W = 4, parameter int LO = 0)
    (input logic [W-1:0] a, output logic [W-1:0] up, output logic [W-1:0] down,
     output logic [W-1:0] slow);
    localparam int HI = LO + W - 1;
    not inv_up [LO:HI] (up, a);
    buf buf_dn [HI:LO] (down, a);
    // A delayed array: each element has a rise and a fall delay.
    buf #(2, 3) buf_slow [LO:HI] (slow, a);
endmodule

// An instance name that is an escaped identifier holding '.' and '['
// (IEEE 1800-2023 5.6.1) names an array with non-literal bounds too.
module esc #(parameter int N = 3) (input logic [N-1:0] a, output logic [N-1:0] y);
    not \inv.a[0]  [0:N-1] (y, a);
endmodule

module top;
    logic [3:0] a = 4'b1010;
    wire [3:0] up4, down4, slow4;
    wire [2:0] up3, down3, slow3;
    logic [2:0] ea = 3'b101;
    wire [2:0] ey;
    esc e (ea, ey);
    // A gate array in the iterations of a for-generate: the arrays belong to
    // the iteration scopes, the loops of their expansion are no scopes.
    logic [1:0] in2 = 2'b10;
    for (genvar g = 0; g < 2; g++) begin : blk
        localparam int HI = g;
        wire [HI:0] w;
        not inv [0:HI] (w, in2[HI:0]);
    end
    // The plug-in lengthens the delays of c4.buf_slow[0] to 8 and 9 before the
    // input falls at time 10, so its output falls at time 19.
    initial #10 a = 4'b0010;
    initial #30 $finish;
    gcell c4 (a, up4, down4, slow4);
    gcell #(.W(3), .LO(2)) c3 (a[2:0], up3, down3, slow3);
endmodule
