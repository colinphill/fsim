// SPDX-License-Identifier: Apache-2.0
`timescale 1ns/1ps
module a2_native_chain;
    logic source;
    wire left_branch;
    wire right_branch;
    wire merged;
    wire visible;
    integer visible_events = 0;

    // Only visible crosses into the checking processes. Intermediate wires
    // remain eligible for the compiled region without hiding an observer.
    assign left_branch = source;
    assign right_branch = source;
    assign merged = left_branch & right_branch;
    assign visible = merged;

    always @(visible)
        visible_events = visible_events + 1;

    initial begin
        source = 1'b0;
        #1;
        visible_events = 0;
        source = 1'b1;
        #1;
        source = 1'b0;
        #1;
        source = 1'b1;
        #1;
        // No change means no further value event.
        source = 1'b1;
        #1;
        if (visible !== 1'b1 || visible_events != 3)
            $fatal(1, "A2 native chain witness failed");
        $display("A2 native visible=1 visible_events=3");
        $finish;
    end
endmodule
