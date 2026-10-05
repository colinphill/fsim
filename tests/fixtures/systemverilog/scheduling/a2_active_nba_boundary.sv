// SPDX-License-Identifier: Apache-2.0
`timescale 1ns/1ps
module a2_active_nba_boundary;
    logic active_input;
    logic nba_input;
    wire result;

    assign result = active_input & nba_input;

    initial begin
        active_input = 1'b0;
        nba_input = 1'b0;
        #1;

        active_input = 1'b1;
        nba_input <= 1'b1;
        #0;
        $display("A2 INACTIVE result=%0b active=%0b nba=%0b",
            result, active_input, nba_input);
        $strobe("A2 STROBE result=%0b active=%0b nba=%0b",
            result, active_input, nba_input);
        #1;
        $finish;
    end
endmodule
