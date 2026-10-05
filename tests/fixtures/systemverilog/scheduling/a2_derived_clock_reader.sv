// SPDX-License-Identifier: Apache-2.0
`timescale 1ns/1ps
module a2_derived_clock_reader;
    logic data;
    logic source_clock;
    logic sampled;
    wire clock_stage;
    wire derived_clock;

    assign clock_stage = source_clock;
    assign derived_clock = clock_stage;

    always @(posedge derived_clock)
        sampled <= data;

    initial begin
        data = 1'b0;
        source_clock = 1'b0;
        sampled = 1'b0;
        #1;

        // The edge passes through two continuous assignments.  The reader
        // must sample before the later Inactive-region data change.
        data = 1'b1;
        source_clock = 1'b1;
        #0;
        data = 1'b0;
        #1;

        if (sampled !== 1'b1 || data !== 1'b0 || derived_clock !== 1'b1)
            $fatal(1, "A2 derived-clock reader observed the wrong data");
        $display("A2 DERIVED sampled=1 data=0 clock=1");
        $finish;
    end
endmodule
