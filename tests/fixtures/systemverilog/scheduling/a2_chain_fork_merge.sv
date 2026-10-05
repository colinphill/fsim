// SPDX-License-Identifier: Apache-2.0
`timescale 1ns/1ps
module a2_chain_fork_merge;
    logic source;
    logic unrelated;
    wire left_branch;
    wire right_branch;
    wire quiet_branch;
    wire merged;
    wire visible;
    integer merged_events = 0;
    integer quiet_events = 0;
    integer visible_events = 0;
    integer unrelated_runs = 0;

    assign left_branch = source;
    assign right_branch = source;
    assign quiet_branch = source & 1'b0;
    assign merged = (left_branch & right_branch) | quiet_branch;
    assign visible = source;

    always @(merged)
        merged_events = merged_events + 1;

    always @(quiet_branch)
        quiet_events = quiet_events + 1;

    always @(visible)
        visible_events = visible_events + 1;

    always @(unrelated)
        unrelated_runs = unrelated_runs + 1;

    initial begin
        source = 1'b0;
        unrelated = 1'b0;
        #1;
        merged_events = 0;
        quiet_events = 0;
        visible_events = 0;
        unrelated_runs = 0;

        source = 1'b1;
        $display("A2 ACTIVE marker=after-source");
        unrelated = 1'b1;
        $display("A2 ACTIVE marker=after-unrelated");
        #1;

        // Reassigning the same values must not create new value events.
        source = 1'b1;
        unrelated = 1'b1;
        #1;

        if (left_branch !== 1'b1 || right_branch !== 1'b1
            || quiet_branch !== 1'b0 || merged !== 1'b1
            || visible !== 1'b1 || merged_events != 1
            || quiet_events != 0 || visible_events != 1
            || unrelated_runs != 1)
            $fatal(1, "A2 chain/fork/merge witness failed");
        $display("A2 cone visible=1 merged=1 merged_events=1 quiet_events=0 unrelated_runs=1");
        $finish;
    end
endmodule
